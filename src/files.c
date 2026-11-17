#define _XOPEN_SOURCE 700

#include "files.h"
#include "mime.h"
#include "server.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WEB_ROOT "./www"

static const char *status_text(int status) {
    switch (status) {
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 414: return "URI Too Long";
        case 431: return "Request Header Fields Too Large";
        default: return "Internal Server Error";
    }
}

int open_public_file(const char *uri, FileResource *resource) {
    if (resource == NULL || uri == NULL || uri[0] != '/') return 400;
    resource->fd = -1;
    resource->size = 0;
    resource->mime_type = NULL;

    size_t uri_len = strcspn(uri, "?#");
    char adjusted_uri[MAX_URI_LEN];
    int n;
    if (uri_len == 1) {
        n = snprintf(adjusted_uri, sizeof(adjusted_uri), "%s/index.html", WEB_ROOT);
    } else {
        n = snprintf(adjusted_uri, sizeof(adjusted_uri), "%s%.*s",
                     WEB_ROOT, (int)uri_len, uri);
    }
    if (n < 0 || (size_t)n >= sizeof(adjusted_uri)) return 414;

    char real_web_root[PATH_MAX];
    if (realpath(WEB_ROOT, real_web_root) == NULL) return 500;

    char real_path[PATH_MAX];
    if (realpath(adjusted_uri, real_path) == NULL) {
        if (errno == EACCES) return 403;
        if (errno == ENOENT || errno == ENOTDIR) return 404;
        return 500;
    }

    size_t root_len = strlen(real_web_root);
    if (strncmp(real_path, real_web_root, root_len) != 0 ||
        (real_path[root_len] != '/' && real_path[root_len] != '\0')) {
        return 403;
    }

    int fd = open(real_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd == -1) {
        if (errno == EACCES || errno == ELOOP) return 403;
        if (errno == ENOENT || errno == ENOTDIR) return 404;
        return 500;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return 500;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 0) {
        close(fd);
        return 404;
    }

    resource->fd = fd;
    resource->size = st.st_size;
    resource->mime_type = get_mime_type(real_path);
    return 200;
}

int build_file_header(const FileResource *resource, char *buffer,
                      size_t capacity, size_t *length) {
    if (resource == NULL || buffer == NULL || length == NULL) return -1;
    int n = snprintf(buffer, capacity,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %jd\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     resource->mime_type, (intmax_t)resource->size);
    if (n < 0 || (size_t)n >= capacity) return -1;
    *length = (size_t)n;
    return 0;
}

int build_error_response(int status_code, char *buffer,
                         size_t capacity, size_t *length) {
    if (buffer == NULL || length == NULL) return -1;
    const char *reason = status_text(status_code);
    const char *extra = status_code == 405 ? "Allow: GET\r\n" : "";
    char body[256];
    int body_len = snprintf(body, sizeof(body),
                            "<html><body><h1>%d %s</h1></body></html>",
                            status_code, reason);
    if (body_len < 0 || (size_t)body_len >= sizeof(body)) return -1;

    int n = snprintf(buffer, capacity,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: text/html; charset=UTF-8\r\n"
                     "Content-Length: %d\r\n"
                     "%s"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     status_code, reason, body_len, extra, body);
    if (n < 0 || (size_t)n >= capacity) return -1;
    *length = (size_t)n;
    return 0;
}
