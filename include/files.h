#ifndef FILES_H
#define FILES_H

void serve_file(int client_fd, const char *uri, int keep_alive);
void send_error_response(int client_fd, int status_code, const char *message, int keep_alive);

#endif