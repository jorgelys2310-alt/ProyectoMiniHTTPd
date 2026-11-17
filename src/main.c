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

typedef struct {
    int fd;
    char request[BUFFER_SIZE];
    size_t received;

    char response[BUFFER_SIZE];
    size_t response_length;
    size_t response_sent;

    FileResource file;
    char file_buffer[BUFFER_SIZE];
    size_t chunk_length;
    size_t chunk_sent;
} Client;

static void close_client(int epoll_fd, Client *client) {
    if (client == NULL) return;
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    if (client->file.fd >= 0) close(client->file.fd);
    close(client->fd);
    printf("Cliente %d desconectado\n", client->fd);
    fflush(stdout);
    free(client);
}

static int enable_write(int epoll_fd, Client *client) {
    struct epoll_event event = {0};
    event.events = EPOLLOUT;
    event.data.ptr = client;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &event);
}

static int prepare_response(Client *client, int status, const char *uri) {
    if (status == 200) {
        status = open_public_file(uri, &client->file);
    }
    if (status == 200) {
        if (build_file_header(&client->file, client->response,
                              sizeof(client->response),
                              &client->response_length) == -1) {
            return -1;
        }
    } else {
        if (build_error_response(status, client->response,
                                 sizeof(client->response),
                                 &client->response_length) == -1) {
            return -1;
        }
    }
    client->response_sent = 0;
    printf("Estado HTTP: %d\n", status);
    return 0;
}

static int write_client(Client *client) {
    if (client->response_sent < client->response_length) {
        ssize_t n = send_data(client->fd,
                              client->response + client->response_sent,
                              client->response_length - client->response_sent);
        if (n > 0) {
            client->response_sent += (size_t)n;
            return 0;
        }
        if (n == -1 && (errno == EINTR || errno == EAGAIN ||
                        errno == EWOULDBLOCK)) return 0;
        return -1;
    }

    if (client->file.fd == -1) return 1; /* respuesta de error completa */

    if (client->chunk_sent == client->chunk_length) {
        ssize_t n = read(client->file.fd, client->file_buffer,
                         sizeof(client->file_buffer));
        if (n > 0) {
            client->chunk_length = (size_t)n;
            client->chunk_sent = 0;
        } else if (n == 0) {
            return 1;
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
        return 0;
    }
    if (n == -1 && (errno == EINTR || errno == EAGAIN ||
                    errno == EWOULDBLOCK)) return 0;
    return -1;
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
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
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

                    Client *client = calloc(1, sizeof(*client));
                    if (client == NULL) {
                        perror("calloc cliente");
                        close(client_fd);
                        continue;
                    }
                    client->fd = client_fd;
                    client->file.fd = -1;

                    struct epoll_event event = {0};
                    event.events = EPOLLIN | EPOLLRDHUP;
                    event.data.ptr = client;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                        perror("epoll_ctl cliente");
                        close(client_fd);
                        free(client);
                        continue;
                    }
                    printf("Cliente %d conectado\n", client_fd);
                    fflush(stdout);
                }
                continue;
            }

            Client *client = events[i].data.ptr;
            int must_close = 0;
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                must_close = 1;
            } else if (events[i].events & EPOLLIN) {
                size_t space = sizeof(client->request) - client->received - 1;
                if (space == 0) {
                    if (prepare_response(client, 431, NULL) == -1 ||
                        enable_write(epoll_fd, client) == -1) must_close = 1;
                } else {
                    ssize_t n = receive_client_data(client->fd,
                        client->request + client->received, space);
                    if (n > 0) {
                        client->received += (size_t)n;
                        client->request[client->received] = '\0';
                        if (strstr(client->request, "\r\n\r\n") != NULL) {
                            HttpRequest req;
                            int status = parse_http_request(client->request, &req);
                            if (status == 200) {
                                printf("Metodo: %s\nURI: %s\nVersion: %s\n",
                                       req.method, req.uri, req.version);
                            }
                            if (prepare_response(client, status,
                                                 status == 200 ? req.uri : NULL) == -1 ||
                                enable_write(epoll_fd, client) == -1) {
                                must_close = 1;
                            }
                        } else if (client->received == sizeof(client->request) - 1) {
                            if (prepare_response(client, 431, NULL) == -1 ||
                                enable_write(epoll_fd, client) == -1) must_close = 1;
                        }
                    } else if (n == 0) {
                        must_close = 1;
                    } else if (errno != EINTR && errno != EAGAIN &&
                               errno != EWOULDBLOCK) {
                        perror("recv");
                        must_close = 1;
                    }
                }
            } else if (events[i].events & EPOLLOUT) {
                int result = write_client(client);
                if (result == 1 || result == -1) must_close = 1;
            }
            if (must_close) close_client(epoll_fd, client);
        }
    }

    close(server_fd);
    close(epoll_fd);
    return EXIT_FAILURE;
}
