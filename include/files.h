/**
 * @file files.h
 * @brief Acceso seguro a los archivos estáticos de la carpeta www/.
 */

#ifndef FILES_H
#define FILES_H

#include <stddef.h>
#include <sys/types.h>

/**
 * @brief Archivo abierto listo para enviarse al cliente.
 */
typedef struct {
    int fd;                 /**< Descriptor del archivo, o -1 si no hay archivo. */
    off_t size;             /**< Tamaño en bytes (se usa en Content-Length). */
    const char *mime_type;  /**< Tipo de contenido (Content-Type). */
} FileResource;

/**
 * @brief Abre el archivo de www/ que corresponde a una URI.
 *
 * Ignora la query string, decodifica %XX, bloquea el directory traversal
 * (segmentos ".." y verificación con realpath()) y sirve index.html para
 * las rutas que terminan en '/'.
 *
 * @param uri      URI de la solicitud, ej. "/img/foto.jpg".
 * @param resource Salida: descriptor, tamaño y tipo MIME del archivo.
 * @return 200 si se abrió, 400 (URI inválida), 403 (fuera de www/ o sin
 *         permisos), 404 (no existe) o 500 (error interno).
 */
int open_public_file(const char *uri, FileResource *resource);

#endif
