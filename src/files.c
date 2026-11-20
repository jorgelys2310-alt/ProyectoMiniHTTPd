/**
 * @file files.c
 * @brief Acceso seguro al contenido estático de la carpeta www/.
 */

#define _XOPEN_SOURCE 700

#include "files.h"
#include "mime.h"
#include "server.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Carpeta raíz de los archivos públicos (relativa al directorio de ejecución) */
#define WEB_ROOT "./www"

/* Valor de un dígito hexadecimal, o -1 si no lo es. */
static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodifica secuencias %XX (ej: %20 -> ' ', %2e -> '.').
   Devuelve -1 si hay una secuencia inválida, un %00 o no cabe en dst. */
static int percent_decode(const char *src, size_t len, char *dst, size_t capacity) {
    size_t j = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = src[i];
        if (c == '%') {
            if (i + 2 >= len) return -1;
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            if (hi < 0 || lo < 0) return -1;
            c = (char)(hi * 16 + lo);
            if (c == '\0') return -1;
            i += 2;
        }
        if (j + 1 >= capacity) return -1;
        dst[j++] = c;
    }
    dst[j] = '\0';
    return 0;
}

/* ¿La ruta contiene un segmento ".."? (ej: "/../etc", "/a/../../b") */
static int has_dot_dot_segment(const char *path) {
    const char *p = path;
    while ((p = strstr(p, "..")) != NULL) {
        int starts_segment = (p == path) || p[-1] == '/';
        int ends_segment = p[2] == '\0' || p[2] == '/';
        if (starts_segment && ends_segment) return 1;
        p += 2;
    }
    return 0;
}

/*
 * Pasos para servir un archivo de forma segura:
 *   1. Quitar query string y decodificar %XX.
 *   2. Rechazar segmentos ".." (directory traversal).
 *   3. Construir la ruta ./www/... (index.html si termina en '/').
 *   4. Resolver la ruta real con realpath() y comprobar que está dentro de www/.
 *   5. Abrir el archivo y verificar que sea un archivo regular.
 */
int open_public_file(const char *uri, FileResource *resource) {
    if (resource == NULL) return 500;
    resource->fd = -1;
    resource->size = 0;
    resource->mime_type = NULL;
    if (uri == NULL || uri[0] != '/') return 400;

    /* Se ignora la query string (?a=b) y el fragmento (#x) */
    size_t uri_len = strcspn(uri, "?#");

    char decoded[MAX_URI_LEN];
    if (percent_decode(uri, uri_len, decoded, sizeof(decoded)) == -1) return 400;

    /* Directory traversal: primera barrera, se rechaza cualquier ".."
       (incluido "%2e%2e" ya decodificado). realpath() es la segunda. */
    if (has_dot_dot_segment(decoded)) return 403;

    /* "/" o "/carpeta/" -> se sirve el index.html de esa carpeta */
    size_t decoded_len = strlen(decoded);
    const char *index = decoded[decoded_len - 1] == '/' ? "index.html" : "";

    char adjusted_uri[PATH_MAX];
    int n = snprintf(adjusted_uri, sizeof(adjusted_uri), "%s%s%s",
                     WEB_ROOT, decoded, index);
    if (n < 0 || (size_t)n >= sizeof(adjusted_uri)) return 400;

    /* Ruta absoluta real de www/ y del archivo pedido (sin "..", sin enlaces) */
    char real_web_root[PATH_MAX];
    if (realpath(WEB_ROOT, real_web_root) == NULL) return 500;

    char real_path[PATH_MAX];
    if (realpath(adjusted_uri, real_path) == NULL) {
        if (errno == EACCES) return 403;
        if (errno == ENAMETOOLONG) return 400;   /* nombre/URI demasiado largo */
        if (errno == ENOENT || errno == ENOTDIR) return 404;
        return 500;
    }

    /* El archivo debe estar dentro de www/: "/ruta/www" o "/ruta/www/..." */
    size_t root_len = strlen(real_web_root);
    if (strncmp(real_path, real_web_root, root_len) != 0 ||
        (real_path[root_len] != '/' && real_path[root_len] != '\0')) {
        return 403;
    }

    /* O_NOFOLLOW: no seguir un enlace simbólico creado después de realpath() */
    int fd = open(real_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd == -1) {
        if (errno == EACCES || errno == ELOOP) return 403;
        if (errno == ENOENT || errno == ENOTDIR) return 404;
        return 500;
    }

    /* Solo se sirven archivos regulares (no carpetas ni dispositivos) */
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
