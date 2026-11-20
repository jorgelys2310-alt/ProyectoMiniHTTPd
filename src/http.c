/**
 * @file http.c
 * @brief Parsing de la solicitud GET y generación de la respuesta HTTP.
 *
 * Solo se usan funciones con límite de tamaño (snprintf, memcpy con
 * longitud verificada, sscanf con anchos) para evitar desbordamientos.
 */

#define _POSIX_C_SOURCE 200809L

#include "http.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* Texto estándar de cada código de estado (ej. 404 -> "Not Found"). */
static const char *status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        default:  return "Internal Server Error";
    }
}

/* Copia el valor de un encabezado sin los espacios del inicio/fin.
   Devuelve -1 si el valor no cabe en dst (encabezado demasiado grande). */
static int copy_header_value(const char *value, char *dst, size_t capacity) {
    while (*value == ' ' || *value == '\t') value++;
    size_t len = strlen(value);
    while (len > 0 && (value[len - 1] == ' ' || value[len - 1] == '\t')) len--;
    if (len >= capacity) return -1;
    memcpy(dst, value, len);
    dst[len] = '\0';
    return 0;
}

/* ¿Aparece token en una lista separada por comas? Ej: "keep-alive, Upgrade" */
static int has_token(const char *list, const char *token) {
    size_t token_len = strlen(token);
    const char *p = list;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        const char *start = p;
        while (*p != '\0' && *p != ',') p++;
        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if ((size_t)(end - start) == token_len &&
            strncasecmp(start, token, token_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * Analiza la solicitud en tres pasos:
 *   1. Línea de solicitud: "MÉTODO URI VERSIÓN" (solo GET, HTTP/1.0 o 1.1).
 *   2. Encabezados: formato "Nombre: valor", con límites de tamaño y cantidad.
 *   3. Validación final: HTTP/1.1 exige el encabezado Host.
 * Se trabaja sobre una copia local para no modificar el buffer del cliente.
 */
int parse_http_request(const char *raw_request, HttpRequest *req) {

    if (raw_request == NULL || req == NULL) return 400;

    if (strlen(raw_request) >= BUFFER_SIZE) return 400;

    char buffer_copy[BUFFER_SIZE];
    snprintf(buffer_copy, sizeof(buffer_copy), "%s", raw_request);

    /* La solicitud termina en una línea vacía (\r\n\r\n) */
    char *end_headers = strstr(buffer_copy, "\r\n\r\n");
    if (end_headers == NULL) return 400;
    *end_headers = '\0';

    char *saveptr = NULL;
    char *line = strtok_r(buffer_copy, "\r\n", &saveptr);
    if (!line) return 400;

    /* Límite de tamaño de la línea de solicitud */
    if (strlen(line) > MAX_REQUEST_LINE) return 400;

    char method[16], uri[MAX_URI_LEN], version[16];
    int consumed = 0;

    /* Si la URI supera MAX_URI_LEN-1 el resto queda sin consumir -> 400 */
    if (sscanf(line, "%15s %1023s %15s %n",
               method, uri, version, &consumed) != 3 ||
        line[consumed] != '\0') {
        return 400;
    }

    /* Solo se acepta GET: cualquier otro método -> 405 */
    if (strcmp(method, "GET") != 0) {
        return 405;
    }

    if (strcmp(version, "HTTP/1.0") != 0 &&
        strcmp(version, "HTTP/1.1") != 0) {
        return 400;
    }

    /* La URI debe ser una ruta absoluta */
    if (uri[0] != '/') return 400;

    /* Solicitud válida hasta aquí: se guardan los datos y valores por defecto */
    snprintf(req->method, sizeof(req->method), "%s", method);
    snprintf(req->uri, sizeof(req->uri), "%s", uri);
    snprintf(req->version, sizeof(req->version), "%s", version);

    req->host[0] = '\0';
    req->user_agent[0] = '\0';
    req->keep_alive = (strcmp(version, "HTTP/1.1") == 0);

    snprintf(req->connection, sizeof(req->connection), "%s",
             req->keep_alive ? "keep-alive" : "close");

    int header_count = 0;
    int host_seen = 0;

    /* Recorrido de los encabezados, una línea a la vez */
    while ((line = strtok_r(NULL, "\r\n", &saveptr)) != NULL) {

        if (++header_count > MAX_HEADERS) return 400;
        if (strlen(line) > MAX_HEADER_LINE) return 400;

        if (line[0] == ' ' || line[0] == '\t') return 400;
        char *colon = strchr(line, ':');
        if (colon == NULL || colon == line) return 400;
        *colon = '\0';
        if (strpbrk(line, " \t") != NULL) return 400;

        const char *name = line;
        const char *value = colon + 1;

        if (strcasecmp(name, "Host") == 0) {
            if (host_seen) return 400;
            host_seen = 1;
            if (copy_header_value(value, req->host, sizeof(req->host)) == -1 ||
                req->host[0] == '\0' || strpbrk(req->host, " \t") != NULL) {
                return 400;
            }
        }
        else if (strcasecmp(name, "Connection") == 0) {
            if (copy_header_value(value, req->connection,
                                  sizeof(req->connection)) == -1) {
                return 400;
            }

            if (has_token(req->connection, "close")) {
                req->keep_alive = 0;
            }
            else if (has_token(req->connection, "keep-alive")) {
                req->keep_alive = 1;
            }
        }
        else if (strcasecmp(name, "User-Agent") == 0) {
            if (copy_header_value(value, req->user_agent,
                                  sizeof(req->user_agent)) == -1) {
                return 400;
            }
        }
        else if (strcasecmp(name, "Transfer-Encoding") == 0) {
            return 400;
        }
        else if (strcasecmp(name, "Content-Length") == 0) {
            char length[32];
            if (copy_header_value(value, length, sizeof(length)) == -1 ||
                strcmp(length, "0") != 0) {
                return 400;
            }
        }
    }

    if (strcmp(version, "HTTP/1.1") == 0 &&
        req->host[0] == '\0') {
        return 400;
    }

    return 200;
}

/* Cabeceras de una respuesta 200 OK; el cuerpo (archivo) se envía aparte. */
int build_file_header(const FileResource *resource, int keep_alive, char *buffer,
                      size_t capacity, size_t *length) {
    if (resource == NULL || buffer == NULL || length == NULL) return -1;
    int n = snprintf(buffer, capacity,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %jd\r\n"
                     "Connection: %s\r\n"
                     "\r\n",
                     resource->mime_type, (intmax_t)resource->size,
                     keep_alive ? "keep-alive" : "close");
    if (n < 0 || (size_t)n >= capacity) return -1;
    *length = (size_t)n;
    return 0;
}

/* Respuesta de error completa: cabeceras + pequeña página HTML. */
int build_error_response(int status_code, int keep_alive, char *buffer,
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
                     "Connection: %s\r\n"
                     "\r\n"
                     "%s",
                     status_code, reason, body_len, extra,
                     keep_alive ? "keep-alive" : "close", body);
    if (n < 0 || (size_t)n >= capacity) return -1;
    *length = (size_t)n;
    return 0;
}