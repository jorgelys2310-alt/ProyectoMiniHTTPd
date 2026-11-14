#include "server.h"
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

static void close_client(int epoll_fd, int client_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
    printf("Cliente %d desconectado\n", client_fd);
    fflush(stdout);
}

int main(){
    int server_fd = start_server();
    if (server_fd == -1) {
        return EXIT_FAILURE;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event ev = {0};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl (servidor)");
        close(epoll_fd);
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event events[MAX_EVENTS];
    printf("MiniHTTPd corriendo en http://127.0.0.1:%d/\n", PORT);

    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            if (fd == server_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                
                if (client_fd == -1) {
                        if (errno == EINTR) {
                            continue;
                        }
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("accept");
                        }
                        break;
                    }

                set_nonblocking(client_fd);

                struct epoll_event client_ev = {0};
                client_ev.events = EPOLLIN; 
                client_ev.data.fd = client_fd;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) == -1) {
                    perror("Error añadiendo cliente a epoll");
                    close(client_fd);
                }

                printf("Cliente %d conectado\n", client_fd);
                fflush(stdout);

            } else {
                if (events[i].events & EPOLLIN) {
                    int result = receive_client_data(fd);
                    if (result <= 0) {
                        close_client(epoll_fd, fd);
                    }
                } else if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    close_client(epoll_fd, fd);
                }
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return 0;
}