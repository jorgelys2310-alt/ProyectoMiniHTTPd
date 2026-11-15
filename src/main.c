#include "server.h"
#include "http.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>

typedef struct {
    int fd;
    char buffer[BUFFER_SIZE];
    size_t bytes_received;
    char response[BUFFER_SIZE];
    size_t response_length;
    size_t response_sent;
} Client;

static void close_client(int epoll_fd, Client *client) {
    if (client == NULL) return;
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);
    printf("Cliente %d desconectado\n", client->fd);
    free(client);
}

static int prepare_response(Client *client, int status) {
    const char *body;
    const char *status_line;
    const char *type;
    const char *extra = "";

    switch (status) {
        case 200:
            status_line = "HTTP/1.1 200 OK";
            body = "<html><body><h1>200 OK - MiniHTTPd funcionando</h1></body></html>";
            type = "text/html";
            break;
        case 405:
            status_line = "HTTP/1.1 405 Method Not Allowed";
            body = "405 Method Not Allowed\n";
            type = "text/plain";
            extra = "Allow: GET\r\n";
            break;
        default:
            status_line = "HTTP/1.1 400 Bad Request";
            body = "400 Bad Request\n";
            type = "text/plain";
            break;
    }

    int len = snprintf(client->response, sizeof(client->response),
        "%s\r\n"
        "Content-Type: %s; charset=UTF-8\r\n"
        "Content-Length: %zu\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status_line, type, strlen(body), extra, body);

    if (len < 0 || (size_t)len >= sizeof(client->response)) return -1;
    client->response_length = (size_t)len;
    client->response_sent = 0;
    return 0;
}

int main(void) {
    int server_fd = start_server();
    if (server_fd == -1) return EXIT_FAILURE;

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event ev = {0};
    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl (servidor)");
        close(epoll_fd);
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event events[MAX_EVENTS];
    printf("MiniHTTPd escuchando en 127.0.0.1:%d\n", PORT);
    fflush(stdout);

    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
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
                        perror("fcntl (cliente)");
                        close(client_fd);
                        continue;
                    }

                    Client *client = calloc(1, sizeof(*client));
                    if (client == NULL) {
                        perror("calloc client");
                        close(client_fd);
                        continue;
                    }
                    client->fd = client_fd;
                    struct epoll_event client_ev = {0};
                    client_ev.events = EPOLLIN;
                    client_ev.data.ptr = client;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) == -1) {
                        perror("epoll_ctl (cliente)");
                        close(client_fd);
                        free(client);
                        continue;
                    }
                    printf("Cliente %d conectado\n", client_fd);
                    fflush(stdout);
                }
            } else {
                Client *client = events[i].data.ptr;
                int must_close = 0;

                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    must_close = 1;
                } else if (events[i].events & EPOLLIN) {
                    size_t available = sizeof(client->buffer) - client->bytes_received - 1;
                    if (available == 0) {
                        fprintf(stderr, "Solicitud demasiado grande: cliente %d\n", client->fd);
                        must_close = 1;
                    } else {
                        ssize_t n = receive_client_data(client->fd,
                            client->buffer + client->bytes_received, available);
                        if (n > 0) {
                            client->bytes_received += (size_t)n;
                            client->buffer[client->bytes_received] = '\0';

                            if (strstr(client->buffer, "\r\n\r\n") != NULL) {
                                HttpRequest req;
                                int status = parse_http_request(client->buffer, &req);
                                printf("Estado HTTP: %d\n", status);
                                if (status == 200) {
                                    printf("Metodo: %s\nURI: %s\nVersion: %s\n",
                                        req.method, req.uri, req.version);
                                }
                                if (prepare_response(client, status) == -1) {
                                    must_close = 1;
                                } else {
                                    struct epoll_event write_ev = {0};
                                    write_ev.events = EPOLLOUT;
                                    write_ev.data.ptr = client;
                                    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD,
                                                  client->fd, &write_ev) == -1) {
                                        perror("epoll_ctl (escritura)");
                                        must_close = 1;
                                    }
                                }
                            } else if (client->bytes_received == sizeof(client->buffer) - 1) {
                                fprintf(stderr, "Solicitud demasiado grande: cliente %d\n", client->fd);
                                must_close = 1;
                            }
                        } else if (n == 0) {
                            must_close = 1;
                        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("recv");
                            must_close = 1;
                        }
                    }
                } else if (events[i].events & EPOLLOUT) {
                    size_t pending = client->response_length - client->response_sent;
                    ssize_t n = send_data(client->fd,
                        client->response + client->response_sent, pending);
                    if (n > 0) {
                        client->response_sent += (size_t)n;
                        if (client->response_sent == client->response_length) {
                            must_close = 1;
                        }
                    } else if (n == 0) {
                        must_close = 1;
                    } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("send");
                        must_close = 1;
                    }
                }
                if (must_close) close_client(epoll_fd, client);
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return EXIT_FAILURE;
}