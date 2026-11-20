/**
 * @file mime.h
 * @brief Tipos de contenido (MIME) según la extensión del archivo.
 */

#ifndef MIME_H
#define MIME_H

/**
 * @brief Devuelve el tipo MIME de un archivo según su extensión.
 * @param path Ruta del archivo.
 * @return Tipo MIME, ej. "text/html"; "application/octet-stream" si la
 *         extensión no se reconoce.
 */
const char* get_mime_type(const char *path);

#endif
