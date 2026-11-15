
#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include <sys/types.h>

#define PORT 8080
#define MAX_EVENTS 64
#define BUFFER_SIZE 4096
#define MAX_URI_LEN 1024

int set_nonblocking(int sock);
int start_server(void);

ssize_t receive_client_data(int client_fd, char *buffer, size_t capacity);

#endif
