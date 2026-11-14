#ifndef SERVER_H
#define SERVER_H

#ifndef PORT
#define PORT 8080
#endif
#define MAX_EVENTS 64
#define BUFFER_SIZE 4096
#define MAX_URI_LEN 1024

int set_nonblocking(int sock);
int start_server(void);
int receive_client_data(int client_fd);

#endif