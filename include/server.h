/**
 * @file server.h
 * @brief Configuración general del servidor y funciones de sockets.
 *
 * Define el puerto, los tamaños de buffer y los límites de la solicitud,
 * y declara las funciones de bajo nivel para crear el socket del servidor
 * y enviar/recibir datos de los clientes.
 */

#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include <sys/types.h>

#define PORT 8080          /**< Puerto TCP en el que escucha el servidor. */
#define MAX_EVENTS 64      /**< Eventos máximos devueltos por epoll_wait(). */
#define BUFFER_SIZE 4096   /**< Tamaño de los buffers de solicitud/respuesta. */
#define MAX_URI_LEN 1024   /**< Longitud máxima de la URI (incluye '\0'). */
#define MAX_CLIENTS 100    /**< Clientes conectados al mismo tiempo. */

/* Límites de tamaño de la solicitud: si se superan -> 400 Bad Request */
#define MAX_REQUEST_LINE 1100   /**< Longitud máxima de "GET <uri> HTTP/1.1". */
#define MAX_HEADER_LINE  1024   /**< Longitud máxima de cada encabezado. */
#define MAX_HEADERS      50     /**< Número máximo de encabezados. */

/**
 * @brief Pone un socket en modo no bloqueante (O_NONBLOCK).
 * @param sock Descriptor del socket.
 * @return 0 si tuvo éxito, -1 en caso de error.
 */
int set_nonblocking(int sock);

/**
 * @brief Crea el socket del servidor, lo enlaza a 127.0.0.1:PORT y lo pone a escuchar.
 * @return Descriptor del socket de escucha (no bloqueante) o -1 si falla.
 */
int start_server(void);

/**
 * @brief Recibe datos de un cliente (envoltura de recv()).
 * @param client_fd Socket del cliente.
 * @param buffer    Destino de los datos.
 * @param capacity  Bytes máximos a leer.
 * @return Bytes leídos, 0 si el cliente cerró la conexión o -1 en error.
 */
ssize_t receive_client_data(int client_fd, char *buffer, size_t capacity);

/**
 * @brief Envía datos a un cliente sin bloquear y sin generar SIGPIPE.
 * @param fd  Socket del cliente.
 * @param buf Datos a enviar.
 * @param len Cantidad de bytes.
 * @return Bytes enviados (puede ser menos que len) o -1 en error.
 */
ssize_t send_data(int fd, const char *buf, size_t len);

#endif
