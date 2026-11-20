/**
 * @file mime.c
 * @brief Tabla de tipos MIME y búsqueda por extensión.
 */

#include "mime.h"
#include <string.h>
#include <strings.h>

/* Asociación extensión. */
struct mime_map {
    const char *extension;
    const char *mime_type;
};

/* Tabla de tipos soportados; termina con {NULL, NULL} */
static const struct mime_map mime_table[] = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {NULL, NULL}
};

/* Busca la extensión del archivo en la tabla. */
const char *get_mime_type(const char *path) {
    if (path == NULL) return "application/octet-stream";

    /* Solo se mira el nombre del archivo, no las carpetas. */
    const char *slash = strrchr(path, '/');
    const char *filename = slash != NULL ? slash + 1 : path;

    const char *dot = strrchr(filename, '.');
    if (dot == NULL) return "application/octet-stream";

    for (size_t i = 0; mime_table[i].extension != NULL; ++i) {
        if (strcasecmp(dot, mime_table[i].extension) == 0) {
            return mime_table[i].mime_type;
        }
    }
    return "application/octet-stream";
}
