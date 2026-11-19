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

#endif
