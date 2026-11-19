
#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include <sys/types.h>

#define PORT 8080
#define MAX_EVENTS 64
#define BUFFER_SIZE 4096
#define MAX_URI_LEN 1024
#define MAX_CLIENTS 100

#define MAX_REQUEST_LINE 1100   
#define MAX_HEADER_LINE  1024   
#define MAX_HEADERS      50    

int set_nonblocking(int sock);
int start_server(void);
ssize_t receive_client_data(int client_fd, char *buffer, size_t capacity);
ssize_t send_data(int fd, const char *buf, size_t len);

#endif
