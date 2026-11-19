#ifndef HTTP_H
#define HTTP_H

#include "server.h"
#include "files.h"

typedef struct {
    char method[16];
    char uri[MAX_URI_LEN];
    char version[16];
    char host[256];
    char connection[32];
    char user_agent[512];
    int keep_alive;
} HttpRequest;

int parse_http_request(const char *raw_request, HttpRequest *req);

int build_file_header(const FileResource *resource, int keep_alive, char *buffer,
                      size_t capacity, size_t *length);
int build_error_response(int status_code, int keep_alive, char *buffer,
                         size_t capacity, size_t *length);

#endif
