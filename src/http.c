
#include "http.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

int parse_http_request(const char *raw_request, HttpRequest *req) {

    if (raw_request == NULL || req == NULL) return 400;

    if (strlen(raw_request) >= BUFFER_SIZE) return 400;

    char buffer_copy[BUFFER_SIZE];
    snprintf(buffer_copy, sizeof(buffer_copy), "%s", raw_request);

    char *end_headers = strstr(buffer_copy, "\r\n\r\n");
    if (end_headers == NULL) return 400;
    *end_headers = '\0';

    char *line = strtok(buffer_copy, "\r\n");
    if (!line) return 400;

    char method[16], uri[MAX_URI_LEN], version[16];
    int consumed = 0;

    if (sscanf(line, "%15s %1023s %15s %n",
               method, uri, version, &consumed) != 3 ||
        line[consumed] != '\0') {
        return 400;
    }

    if (strcmp(method, "GET") != 0) {
        if (strcmp(method, "POST") == 0 ||
            strcmp(method, "PUT") == 0 ||
            strcmp(method, "DELETE") == 0 ||
            strcmp(method, "HEAD") == 0) {
            return 405;
        }
        return 400;
    }

    if (strcmp(version, "HTTP/1.0") != 0 &&
        strcmp(version, "HTTP/1.1") != 0) {
        return 400;
    }

    snprintf(req->method, sizeof(req->method), "%s", method);
    snprintf(req->uri, sizeof(req->uri), "%s", uri);
    snprintf(req->version, sizeof(req->version), "%s", version);

    req->host[0] = '\0';
    req->keep_alive = (strcmp(version, "HTTP/1.1") == 0);

    snprintf(req->connection, sizeof(req->connection), "%s",
             req->keep_alive ? "keep-alive" : "close");

    while ((line = strtok(NULL, "\r\n")) != NULL) {

        if (strncasecmp(line, "Host:", 5) == 0) {
            if (sscanf(line + 5, " %255s", req->host) != 1) {
                return 400;
            }
        }
        else if (strncasecmp(line, "Connection:", 11) == 0) {
            if (sscanf(line + 11, " %31s", req->connection) != 1) {
                return 400;
            }

            if (strcasecmp(req->connection, "close") == 0) {
                req->keep_alive = 0;
            }
            else if (strcasecmp(req->connection, "keep-alive") == 0) {
                req->keep_alive = 1;
            }
        }
    }

    if (strcmp(version, "HTTP/1.1") == 0 &&
        req->host[0] == '\0') {
        return 400;
    }

    return 200;
}
