/**
 * @file http.h
 * @brief Análisis de solicitudes HTTP y generación de respuestas.
 */

#ifndef HTTP_H
#define HTTP_H

#include "server.h"
#include "files.h"

/**
 * @brief Datos extraídos de una solicitud HTTP válida.
 */
typedef struct {
    char method[16];        /**< Método (siempre "GET" si la solicitud es válida). */
    char uri[MAX_URI_LEN];  /**< Ruta solicitada, ej. "/index.html". */
    char version[16];       /**< "HTTP/1.0" o "HTTP/1.1". */
    char host[256];         /**< Valor del encabezado Host. */
    char connection[32];    /**< Valor del encabezado Connection. */
    char user_agent[512];   /**< Valor del encabezado User-Agent. */
    int keep_alive;         /**< 1 si el cliente quiere mantener la conexión abierta. */
} HttpRequest;

/**
 * @brief Analiza y valida una solicitud HTTP (línea de solicitud + encabezados).
 *
 * Verifica el método, la versión, la URI, el formato y tamaño de los
 * encabezados, y extrae Host, Connection y User-Agent.
 *
 * @param raw_request Texto de la solicitud terminado en '\0'.
 * @param req         Estructura donde se guardan los datos extraídos.
 * @return 200 si es válida, 400 si está mal formada o es demasiado grande,
 *         405 si el método no es GET.
 */
int parse_http_request(const char *raw_request, HttpRequest *req);

/**
 * @brief Construye las cabeceras de una respuesta 200 OK para un archivo.
 * @param resource   Archivo a enviar (tamaño y tipo MIME).
 * @param keep_alive 1 para "Connection: keep-alive", 0 para "close".
 * @param buffer     Destino de las cabeceras.
 * @param capacity   Tamaño de buffer.
 * @param length     Salida: longitud de las cabeceras generadas.
 * @return 0 si tuvo éxito, -1 si no caben en el buffer.
 */
int build_file_header(const FileResource *resource, int keep_alive, char *buffer,
                      size_t capacity, size_t *length);

/**
 * @brief Construye una respuesta de error completa (cabeceras + página HTML).
 *
 * Para el código 405 se añade el encabezado "Allow: GET".
 *
 * @param status_code Código de estado (400, 403, 404, 405 o 500).
 * @param keep_alive  1 para "Connection: keep-alive", 0 para "close".
 * @param buffer      Destino de la respuesta.
 * @param capacity    Tamaño de buffer.
 * @param length      Salida: longitud de la respuesta generada.
 * @return 0 si tuvo éxito, -1 si no cabe en el buffer.
 */
int build_error_response(int status_code, int keep_alive, char *buffer,
                         size_t capacity, size_t *length);

#endif
