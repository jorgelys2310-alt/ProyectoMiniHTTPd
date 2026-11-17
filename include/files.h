#ifndef FILES_H
#define FILES_H

#include <stddef.h>
#include <sys/types.h>

typedef struct {
    int fd;
    off_t size;
    const char *mime_type;
} FileResource;

int open_public_file(const char *uri, FileResource *resource);

int build_file_header(const FileResource *resource, char *buffer,
                      size_t capacity, size_t *length);
int build_error_response(int status_code, char *buffer,
                         size_t capacity, size_t *length);

#endif
