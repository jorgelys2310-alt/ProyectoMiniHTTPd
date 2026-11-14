#include "server.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int set_nonblocking(int sock) {
    int opts = fcntl(sock, F_GETFL, 0);
    if (opts == -1) {
        return -1;
    }
    return fcntl(sock, F_SETFL, opts | O_NONBLOCK);
}

int start_server(){
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(server_fd == -1){
                perror("Error al crear el socket");
                return -1;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(PORT);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0){
                perror("Error en bind");
                goto error;
        }

        if (listen(server_fd, SOMAXCONN) < 0){
                perror("Error en listen");
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

int receive_client_data(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer), 0);

    if (bytes > 0) {
        printf("[cliente %d] %zd bytes recibidos: ", client_fd, bytes);
        fwrite(buffer, 1, (size_t)bytes, stdout);
        putchar('\n');
        fflush(stdout);
        return 1;
    }

    if (bytes == 0) {
        return 0;  /* El cliente cerro su lado de envio. */
    }

    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        return 1;  /* No es un error fatal en un socket no bloqueante. */
    }

    perror("recv");
    return -1;
}