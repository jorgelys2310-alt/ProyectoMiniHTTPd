/**
 * @file main.c
 * @brief Bucle de eventos (epoll) del servidor MiniHTTPd.
 *
 * Atiende muchos clientes en un solo hilo con sockets no bloqueantes.
 * Cada cliente tiene un estado (leyendo, escribiendo o inactivo) y
 * soporta conexiones persistentes (keep-alive) y timeouts.
 */

#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "http.h"
#include "files.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

/* Tiempos de espera en segundos */
#ifndef TIMEOUT_REQUEST
#define TIMEOUT_REQUEST 10
#endif
#ifndef TIMEOUT_KEEPALIVE
#define TIMEOUT_KEEPALIVE 30
#endif
#ifndef TIMEOUT_WRITE
#define TIMEOUT_WRITE 30
#endif

#ifndef MAX_CLIENTS
#define MAX_CLIENTS 100
#endif

/**
 * @brief Estado de cada conexión en la máquina de estados del servidor.
 *
 *   READING_REQUEST --(solicitud completa)--> WRITING_RESPONSE
 *   WRITING_RESPONSE --(enviada, keep-alive)--> IDLE_KEEPALIVE / READING_REQUEST
 *   IDLE_KEEPALIVE --(llegan datos)--> READING_REQUEST
 */
typedef enum {
    CLIENT_READING_REQUEST,
    CLIENT_WRITING_RESPONSE,
    CLIENT_IDLE_KEEPALIVE
} ClientState;

/**
 * @brief Datos de una conexión. Los clientes forman una lista doblemente
 *        enlazada para poder revisar los timeouts de todos.
 */
typedef struct Client {
    int fd;
    /* Solicitud recibida (puede contener varias si hay pipelining) */
    char request[BUFFER_SIZE];
    size_t received;

    /* Cabeceras (o respuesta de error completa) pendientes de enviar */
    char response[BUFFER_SIZE];
    size_t response_length;
    size_t response_sent;

    /* Archivo que se envía por bloques de BUFFER_SIZE bytes */
    FileResource file;
    off_t file_remaining;          /* bytes del archivo que faltan por leer */
    char file_buffer[BUFFER_SIZE];
    size_t chunk_length;
    size_t chunk_sent;

    /* Estado de la conexión y marcas de tiempo para los timeouts */
    int keep_alive;
    int peer_half_closed;
    ClientState state;
    time_t request_started;
    time_t last_activity;

    struct Client *next;
    struct Client *prev;
} Client;

/* Lista de clientes conectados y contador (límite MAX_CLIENTS) */
static Client *client_list_head = NULL;
static size_t active_clients = 0; 

/* Segundos de un reloj monótono (no le afectan los cambios de hora del sistema). */
static time_t monotonic_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return ts.tv_sec;
}

/* Cierra la conexión, la quita de epoll y de la lista, y libera su memoria. */
static void close_client(int epoll_fd, Client *client) {
    if (client == NULL) return;
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    if (client->file.fd >= 0) close(client->file.fd);

    /* Si se cierra con datos sin leer, el kernel envía RST y el cliente puede
       perder la última respuesta (ej. un 400). Se envía FIN y se vacía lo pendiente. */
    shutdown(client->fd, SHUT_WR);
    char discard[1024];
    for (int k = 0; k < 64 &&
         recv(client->fd, discard, sizeof(discard), MSG_DONTWAIT) > 0; ++k) {
    }
    close(client->fd);

    if (client->prev != NULL) {
        client->prev->next = client->next;
    } else {
        client_list_head = client->next;
    }
    if (client->next != NULL) {
        client->next->prev = client->prev;
    }

    if (active_clients > 0) {
        active_clients--;
    }

    printf("Cliente %d desconectado\n", client->fd);
    fflush(stdout);
    free(client);
}

/* Cambia el socket a modo lectura: esperar la siguiente solicitud. */
static int enable_read(int epoll_fd, Client *client) {
    struct epoll_event event = {0};
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.ptr = client;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &event);
}

/* Cambia el socket a modo escritura: avisar cuando se pueda enviar. */
static int enable_write(int epoll_fd, Client *client) {
    struct epoll_event event = {0};
    /* Sin EPOLLRDHUP: si el cliente cierra su lado de escritura (shutdown)
       todavía puede recibir la respuesta completa, no hay que cortarla. */
    event.events = EPOLLOUT;
    event.data.ptr = client;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &event);
}

/*
 * Prepara la respuesta a una solicitud ya analizada. Si el parser devolvió
 * 200 se intenta abrir el archivo; según el resultado se generan las
 * cabeceras 200 OK o una página de error.
 * Retorna 0 si tuvo éxito, -1 en error interno.
 */
static int prepare_response(Client *client, int status, const char *uri) {
    if (status == 200) {
        status = open_public_file(uri, &client->file);
    }
    if (status == 200) {
        client->file_remaining = client->file.size;
        if (build_file_header(&client->file, client->keep_alive,
                              client->response, sizeof(client->response),
                              &client->response_length) == -1) return -1;
    } else {
        /* 403/404 mantienen la conexión persistente; 400/500 la cierran
           (los errores del parser ya llegan con keep_alive = 0). */
        if (status == 400 || status == 500) client->keep_alive = 0;
        if (build_error_response(status, client->keep_alive,
                                 client->response, sizeof(client->response),
                                 &client->response_length) == -1) return -1;
    }
    client->response_sent = 0;
    client->state = CLIENT_WRITING_RESPONSE;
    printf("Estado HTTP: %d\n", status);
    return 0;
}

/*
 * Envía la respuesta de forma no bloqueante, un paso por cada EPOLLOUT:
 * primero las cabeceras y después el archivo por bloques.
 * Retorna 0 si queda algo por enviar, 1 si terminó, -1 si hubo un error.
 */
static int write_client(Client *client) {
    if (client->response_sent < client->response_length) {
        ssize_t n = send_data(client->fd,
                              client->response + client->response_sent,
                              client->response_length - client->response_sent);
        if (n > 0) {
            client->response_sent += (size_t)n;
            client->last_activity = monotonic_seconds();
            return 0;
        }
        if (n == -1 && (errno == EINTR || errno == EAGAIN ||
                        errno == EWOULDBLOCK)) return 0;
        return -1;
    }

    /* Cabeceras enviadas; si no hay archivo (error) la respuesta terminó */
    if (client->file.fd == -1) return 1;

    if (client->chunk_sent == client->chunk_length) {
        if (client->file_remaining == 0) return 1;

        /* No leer más de lo anunciado en Content-Length */
        size_t to_read = sizeof(client->file_buffer);
        if ((off_t)to_read > client->file_remaining) {
            to_read = (size_t)client->file_remaining;
        }
        ssize_t n = read(client->file.fd, client->file_buffer, to_read);
        if (n > 0) {
            client->chunk_length = (size_t)n;
            client->chunk_sent = 0;
            client->file_remaining -= n;
        } else if (n == 0) {
            /* El archivo se acortó mientras se enviaba: Content-Length ya no
               coincide, hay que cerrar para no desincronizar al cliente. */
            fprintf(stderr, "Archivo truncado durante el envio\n");
            return -1;
        } else if (errno == EINTR) {
            return 0;
        } else {
            perror("read archivo");
            return -1;
        }
    }

    ssize_t n = send_data(client->fd,
                          client->file_buffer + client->chunk_sent,
                          client->chunk_length - client->chunk_sent);
    if (n > 0) {
        client->chunk_sent += (size_t)n;
        client->last_activity = monotonic_seconds();
        return 0;
    }
    if (n == -1 && (errno == EINTR || errno == EAGAIN ||
                    errno == EWOULDBLOCK)) return 0;
    return -1;
}

/*
 * Revisa si en el buffer hay una solicitud completa (termina en \r\n\r\n).
 * Si la hay, la analiza, prepara la respuesta y descarta del buffer los
 * bytes usados (lo que sobra puede ser la siguiente solicitud).
 * Retorna 0 si falta información, 1 si hay respuesta lista, -1 en error.
 */
static int process_buffered_request(int epoll_fd, Client *client) {
    /* Un byte nulo dentro de la solicitud no es HTTP válido */
    if (memchr(client->request, '\0', client->received) != NULL) {
        client->keep_alive = 0;
        if (prepare_response(client, 400, NULL) == -1) return -1;
        return enable_write(epoll_fd, client) == -1 ? -1 : 1;
    }

    char *end = strstr(client->request, "\r\n\r\n");
    if (end == NULL && client->received < sizeof(client->request) - 1) {
        return 0;
    }

    if (end == NULL) {
        /* Encabezados demasiado grandes (no caben en el buffer) -> 400 */
        client->keep_alive = 0;
        if (prepare_response(client, 400, NULL) == -1) return -1;
        return enable_write(epoll_fd, client) == -1 ? -1 : 1;
    }

    /* Bytes de esta solicitud, incluido el \r\n\r\n final */
    size_t consumed = (size_t)(end - client->request) + 4;
    HttpRequest req;
    int status = parse_http_request(client->request, &req);

    /* Solo se mantiene la conexión si la solicitud es válida, HTTP/1.1,
       el cliente lo pide y no ha cerrado su lado de la conexión */
    client->keep_alive = status == 200 && req.keep_alive &&
                         strcmp(req.version, "HTTP/1.1") == 0 &&
                         !client->peer_half_closed;

    if (status == 200) {
        printf("Metodo: %s\nURI: %s\nVersion: %s\nHost: %s\nUser-Agent: %s\n",
               req.method, req.uri, req.version, req.host,
               req.user_agent[0] != '\0' ? req.user_agent : "-");
    }
    if (prepare_response(client, status, status == 200 ? req.uri : NULL) == -1) {
        return -1;
    }

    /* Mover al inicio lo que sobra (solicitudes encadenadas / pipelining) */
    size_t leftover = client->received - consumed;
    memmove(client->request, client->request + consumed, leftover);
    client->received = leftover;
    client->request[leftover] = '\0';

    return enable_write(epoll_fd, client) == -1 ? -1 : 1;
}

/*
 * Cierra los clientes que superaron su tiempo de espera según su estado:
 *   - leyendo la solicitud:  TIMEOUT_REQUEST desde que empezó
 *   - escribiendo:           TIMEOUT_WRITE sin progreso
 *   - inactivo (keep-alive): TIMEOUT_KEEPALIVE sin actividad
 */
static void check_timeouts(int epoll_fd) {
    time_t now = monotonic_seconds();
    Client *curr = client_list_head;

    while (curr != NULL) {
        Client *next_client = curr->next;
        int limit;
        time_t since;

        if (curr->state == CLIENT_IDLE_KEEPALIVE) {
            limit = TIMEOUT_KEEPALIVE;
            since = curr->last_activity;
        } else if (curr->state == CLIENT_READING_REQUEST) {
            limit = TIMEOUT_REQUEST;
            since = curr->request_started;
        } else {
            limit = TIMEOUT_WRITE;
            since = curr->last_activity;
        }

        if (now - since > limit) {
            printf("Timeout: cliente %d, estado %d, transcurridos %ld s\n",
                   curr->fd, (int)curr->state, (long)(now - since));
            close_client(epoll_fd, curr);
        }
        curr = next_client;
    }
}

/*
 * Bucle principal (un solo hilo):
 *   1. Crear el socket del servidor y la instancia de epoll.
 *   2. Esperar eventos con epoll_wait() (máximo 1 s para revisar timeouts).
 *   3. Socket del servidor listo -> aceptar clientes nuevos.
 *      Cliente listo -> leer solicitud (EPOLLIN) o enviar respuesta (EPOLLOUT).
 *   4. Revisar timeouts.
 */
int main(void) {
    int server_fd = start_server();
    if (server_fd == -1) return EXIT_FAILURE;

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return EXIT_FAILURE;
    }

    /* El socket del servidor se identifica en epoll con data.ptr = NULL */
    struct epoll_event server_event = {0};
    server_event.events = EPOLLIN;
    server_event.data.ptr = NULL;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &server_event) == -1) {
        perror("epoll_ctl servidor");
        close(epoll_fd);
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event events[MAX_EVENTS];
    printf("MiniHTTPd escuchando en 127.0.0.1:%d\n", PORT);
    fflush(stdout);

    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            /* ---- Conexiones nuevas: aceptar todas las pendientes ---- */
            if (events[i].data.ptr == NULL) {
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd,
                        (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd == -1) {
                        if (errno == EINTR) continue;
                        if (errno != EAGAIN && errno != EWOULDBLOCK) perror("accept");
                        break;
                    }
                    if (set_nonblocking(client_fd) == -1) {
                        perror("fcntl cliente");
                        close(client_fd);
                        continue;
                    }

                    /* Límite de clientes simultáneos */
                    if (active_clients >= MAX_CLIENTS) {
                        printf("Limite de clientes alcanzado\n");
                        close(client_fd);
                        continue;
                    }

                    /* Nuevo cliente: empieza esperando su solicitud */
                    Client *client = calloc(1, sizeof(*client));
                    if (client == NULL) {
                        perror("calloc cliente");
                        close(client_fd);
                        continue;
                    }
                    client->fd = client_fd;
                    client->file.fd = -1;
                    client->keep_alive = 0; 
                    client->state = CLIENT_READING_REQUEST;
                    client->request_started = monotonic_seconds();
                    client->last_activity = client->request_started;

                    struct epoll_event event = {0};
                    event.events = EPOLLIN | EPOLLRDHUP;
                    event.data.ptr = client;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                        perror("epoll_ctl cliente");
                        close(client_fd);
                        free(client);
                        continue;
                    }

                    active_clients++;

                    /* Insertar al inicio de la lista de clientes */
                    client->next = client_list_head;
                    client->prev = NULL;
                    if (client_list_head != NULL) {
                        client_list_head->prev = client;
                    }
                    client_list_head = client;

                    printf("Cliente %d conectado\n", client_fd);
                    fflush(stdout);
                }
                continue;
            }

            /* ---- Evento de un cliente existente ---- */
            Client *client = events[i].data.ptr;
            int must_close = 0;
            if (events[i].events & EPOLLRDHUP) client->peer_half_closed = 1;

            /* Error o conexión cerrada por el cliente */
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                must_close = 1;
            /* Socket listo para escribir: continuar enviando la respuesta */
            } else if (events[i].events & EPOLLOUT) {
                int result = write_client(client);
                if (result == -1) {
                    must_close = 1;
                /* Respuesta enviada por completo: limpiar para la siguiente */
                } else if (result == 1) {
                    if (client->file.fd >= 0) {
                        close(client->file.fd);
                        client->file.fd = -1;
                    }
                    client->response_length = 0;
                    client->response_sent = 0;
                    client->chunk_length = 0;
                    client->chunk_sent = 0;
                    client->last_activity = monotonic_seconds();

                    /* Cerrar o seguir en la misma conexión (keep-alive) */
                    if (!client->keep_alive || client->peer_half_closed) {
                        must_close = 1;
                    } else {
                        if (client->received > 0) {
                            client->state = CLIENT_READING_REQUEST;
                            client->request_started = monotonic_seconds();
                        } else {
                            client->state = CLIENT_IDLE_KEEPALIVE;
                        }
                        /* Puede haber otra solicitud ya recibida (pipelining) */
                        int next = process_buffered_request(epoll_fd, client);
                        if (next == -1) {
                            must_close = 1;
                        } else if (next == 0 && enable_read(epoll_fd, client) == -1) {
                            perror("epoll_ctl EPOLLIN");
                            must_close = 1;
                        }
                    }
                }
            /* Socket con datos: leer y ver si la solicitud está completa */
            } else if (events[i].events & EPOLLIN) {
                size_t space = sizeof(client->request) - client->received - 1;
                if (space == 0) {
                    if (process_buffered_request(epoll_fd, client) == -1)
                        must_close = 1;
                } else {
                    ssize_t n = receive_client_data(client->fd,
                        client->request + client->received, space);
                    if (n > 0) {
                        if (client->state == CLIENT_IDLE_KEEPALIVE) {
                            client->state = CLIENT_READING_REQUEST;
                            client->request_started = monotonic_seconds();
                        }
                        client->received += (size_t)n;
                        client->last_activity = monotonic_seconds();
                        client->request[client->received] = '\0';
                        if (process_buffered_request(epoll_fd, client) == -1)
                            must_close = 1;
                    /* recv() == 0: el cliente cerró la conexión */
                    } else if (n == 0) {
                        must_close = 1;
                    } else if (errno != EINTR && errno != EAGAIN &&
                               errno != EWOULDBLOCK) {
                        perror("recv");
                        must_close = 1;
                    }
                }
            } else if (client->peer_half_closed) {
                must_close = 1;
            }

            if (must_close) close_client(epoll_fd, client);
        }

        check_timeouts(epoll_fd);
    }

    /* Solo se llega aquí si epoll_wait() falla: liberar todo */
    while (client_list_head != NULL) close_client(epoll_fd, client_list_head);
    close(server_fd);
    close(epoll_fd);
    return EXIT_FAILURE;
}