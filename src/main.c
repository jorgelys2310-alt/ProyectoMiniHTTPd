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
} Client;

static void close_client(int epoll_fd, Client *client) {
    if (client == NULL) return;
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);
    printf("Cliente %d desconectado\n", client->fd);
    free(client);
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
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                            perror("accept");
                        break;
                    }

                    if (set_nonblocking(client_fd) == -1) {
                        perror("fcntl (cliente)");
                        close(client_fd);
                        continue;
                    }

                    Client *client = malloc(sizeof(*client));
                    if (client == NULL) {
                        perror("malloc client");
                        close(client_fd);
                        continue;
                    }
                    client->fd = client_fd;
                    client->bytes_received = 0;
                    client->buffer[0] = '\0';

                    struct epoll_event client_ev = {0};
                    client_ev.events = EPOLLIN | EPOLLRDHUP;
                    client_ev.data.ptr = client;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                                  client_fd, &client_ev) == -1) {
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

                if (events[i].events & EPOLLIN) {
                    size_t available = sizeof(client->buffer) - client->bytes_received - 1;
                    if (available == 0) {
                        fprintf(stderr, "Solicitud demasiado grande: cliente %d\n", client->fd);
                        must_close = 1;
                    } else {
                        ssize_t n = receive_client_data(client->fd, client->buffer + client->bytes_received, available);
                        if (n > 0) {
                            client->bytes_received += (size_t)n;
                            client->buffer[client->bytes_received] = '\0';

                            if (strstr(client->buffer, "\r\n\r\n") != NULL) {
                                HttpRequest req;
                                int status = parse_http_request(client->buffer, &req);
                                printf("Estado HTTP: %d\n", status);
                                if (status == 200) {
                                    printf("Metodo: %s\n", req.method);
                                    printf("URI: %s\n", req.uri);
                                    printf("Version: %s\n", req.version);
                                }
                                must_close = 1;
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
                }
                if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    must_close = 1;
                }
                if (must_close) close_client(epoll_fd, client);
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return EXIT_FAILURE;
}