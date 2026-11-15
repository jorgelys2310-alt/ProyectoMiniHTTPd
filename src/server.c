#include "server.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

int set_nonblocking(int sock) {
    int opts = fcntl(sock, F_GETFL, 0);
    if (opts == -1) return -1;
    return fcntl(sock, F_SETFL, opts | O_NONBLOCK);
}

int start_server(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        goto error;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        goto error;
    }
    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("listen");
        goto error;
    }
    if (set_nonblocking(server_fd) == -1) {
        perror("fcntl (servidor)");
        goto error;
    }
    return server_fd;

error:
    close(server_fd);
    return -1;
}

ssize_t receive_client_data(int client_fd, char *buffer, size_t capacity) {
    return recv(client_fd, buffer, capacity, 0);
}

ssize_t send_data(int fd, const char *buf, size_t len) {
    return send(fd, buf, len, MSG_NOSIGNAL);
}
