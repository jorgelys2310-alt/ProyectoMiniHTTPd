#ifndef HTTP_H
#define HTTP_H

#include "server.h"

typedef struct {
    char method[16];
    char uri[MAX_URI_LEN];
    char version[16];
    char host[256];
    char connection[32];
    int keep_alive;
} HttpRequest;

int parse_http_request(const char *raw_request, HttpRequest *req);
void handle_client_request(int client_fd);

#endif