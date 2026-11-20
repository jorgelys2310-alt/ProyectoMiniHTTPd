# MiniHTTPd

Servidor HTTP/1.1 básico en C para Linux, sin bibliotecas HTTP externas.
Sirve archivos estáticos desde la carpeta `www/` y atiende varios clientes a la vez usando `epoll`.

---

## Características

- Servidor de archivos estáticos (HTML, CSS, JavaScript, PNG, JPG) desde la carpeta `www/`.
- Solo acepta el método **GET**; cualquier otro método responde `405 Method Not Allowed`.
- Análisis de los encabezados `Host`, `Connection` y `User-Agent`.
- Soporte para **HTTP/1.0** y **HTTP/1.1** (en HTTP/1.1 el encabezado `Host` es obligatorio).
- Arquitectura **event-driven** con `epoll` en un único hilo, con sockets no bloqueantes.
- **Conexiones persistentes** (keep-alive) y solicitudes encadenadas en una misma conexión (pipelining).
- Envío de archivos grandes por bloques, sin cargarlos completos en memoria.
- Tiempos de espera (timeouts) para clientes inactivos o lentos.
- Límite de clientes simultáneos.
- Tipos MIME según la extensión del archivo.
- Protección contra *directory traversal*, desbordamientos de buffer y solicitudes demasiado grandes.

---

## Estructura

```
minihttpd/
├── Makefile
├── README.md
├── include/
│   ├── http.h      # Estructura HttpRequest y funciones de parsing/respuesta
│   ├── server.h    # Constantes (puerto, límites) y funciones de sockets
│   ├── mime.h      # Función de tipos MIME
│   └── files.h     # Estructura FileResource y acceso a archivos
├── src/
│   ├── main.c      # Bucle de eventos (epoll), estados de clientes y timeouts
│   ├── server.c    # Creación del socket, modo no bloqueante, send/recv
│   ├── http.c      # Parsing de la solicitud GET y generación de la respuesta
│   ├── mime.c      # Tabla de tipos de contenido
│   └── files.c     # Acceso seguro al contenido estático de www/
└── www/            # Raíz de los archivos públicos
```

| Módulo     | Responsabilidad |
|------------|-----------------|
| `main.c`   | Bucle `epoll`, aceptación de clientes, máquina de estados de cada conexión, timeouts. |
| `server.c` | Socket de escucha (`socket`, `bind`, `listen`), `O_NONBLOCK`, envío y recepción. |
| `http.c`   | Valida la línea de solicitud y los encabezados; construye las cabeceras de respuesta y las páginas de error. |
| `files.c`  | Decodifica la URI, resuelve la ruta con `realpath()`, verifica que esté dentro de `www/` y abre el archivo. |
| `mime.c`   | Devuelve el `Content-Type` según la extensión. |

---

## Requisitos

- Linux (usa `epoll`, exclusivo de Linux).
- `gcc` y `make`.
- Opcional, para pruebas: `curl`.

---

## Compilación

```bash
make            # compila y genera el ejecutable ./minihttpd
make clean      # elimina los archivos objeto y el ejecutable
make debug      # compila con AddressSanitizer para detectar errores de memoria
```

## Ejecución

El servidor busca la carpeta `www/` **en el directorio desde el que se ejecuta**,
por lo que debe iniciarse desde la raíz del proyecto:

```bash
make run        # o bien: ./minihttpd
```
Luego abrir en el navegador: <http://127.0.0.1:8080/>

Para detener el servidor: `Ctrl + C`.

---

## Configuración

Los valores se definen en `include/server.h` y `src/main.c`:

| Constante           | Valor  | Descripción |
|---------------------|--------|-------------|
| `PORT`              | 8080   | Puerto de escucha (solo en `127.0.0.1`). |
| `MAX_CLIENTS`       | 100    | Clientes conectados al mismo tiempo. |
| `MAX_EVENTS`        | 64     | Eventos procesados por cada llamada a `epoll_wait`. |
| `BUFFER_SIZE`       | 4096   | Tamaño máximo de la solicitud (línea + encabezados). |
| `MAX_URI_LEN`       | 1024   | Longitud máxima de la URI. |
| `MAX_REQUEST_LINE`  | 1100   | Longitud máxima de la línea de solicitud. |
| `MAX_HEADER_LINE`   | 1024   | Longitud máxima de cada encabezado. |
| `MAX_HEADERS`       | 50     | Número máximo de encabezados. |
| `TIMEOUT_REQUEST`   | 10 s   | Tiempo máximo para recibir una solicitud completa. |
| `TIMEOUT_KEEPALIVE` | 30 s   | Tiempo máximo de una conexión persistente inactiva. |
| `TIMEOUT_WRITE`     | 30 s   | Tiempo máximo sin progreso al enviar una respuesta. |

---

## Funcionamiento

### Bucle de eventos (epoll)

El servidor usa un único hilo. Todos los sockets son **no bloqueantes** y se registran en
una instancia de `epoll`. En cada iteración, `epoll_wait` devuelve los sockets listos:

- **Socket de escucha listo:** se aceptan todas las conexiones pendientes y cada cliente
  se registra con `EPOLLIN`.
- **Cliente con datos (`EPOLLIN`):** se acumulan bytes en su buffer hasta encontrar el fin
  de encabezados (`\r\n\r\n`); entonces se analiza la solicitud y se prepara la respuesta.
- **Cliente listo para escribir (`EPOLLOUT`):** se envía la cabecera y luego el archivo
  por bloques, solo lo que el socket acepte en ese momento.

Después de procesar los eventos se revisan los timeouts de todos los clientes.

### Conexiones persistentes

- En **HTTP/1.1** la conexión se mantiene abierta por defecto, salvo que el cliente envíe
  `Connection: close`.
- En **HTTP/1.0** la conexión se cierra después de cada respuesta.
- Los errores `403` y `404` mantienen la conexión abierta; `400`, `405` y `500` la cierran.
- Si el cliente envía varias solicitudes seguidas (pipelining), se responden en orden.

---

## Métodos y encabezados

| Elemento            | Comportamiento |
|---------------------|----------------|
| `GET`               | Única operación permitida. |
| Otros métodos       | `405 Method Not Allowed` con el encabezado `Allow: GET`. |
| `Host`              | Obligatorio en HTTP/1.1; si falta o está duplicado → `400`. |
| `Connection`        | `close` o `keep-alive`; controla la persistencia. |
| `User-Agent`        | Se lee y se muestra en el registro del servidor. |
| `Content-Length` / `Transfer-Encoding` | Un `GET` con cuerpo se rechaza con `400`. |

## Tipos MIME

| Extensión       | Content-Type               |
|-----------------|----------------------------|
| `.html`         | `text/html`                |
| `.css`          | `text/css`                 |
| `.js`           | `application/javascript`   |
| `.png`          | `image/png`                |
| `.jpg`, `.jpeg` | `image/jpeg`               |
| otra            | `application/octet-stream` |

## Códigos de estado

| Código | Significado           | Cuándo se produce |
|--------|-----------------------|-------------------|
| 200    | OK                    | El archivo existe y se envía. |
| 400    | Bad Request           | Solicitud mal formada, sin `Host` en HTTP/1.1, versión no soportada, URI o encabezados demasiado grandes, `%XX` inválido. |
| 403    | Forbidden             | Intento de salir de `www/` o archivo sin permiso de lectura. |
| 404    | Not Found             | El archivo no existe o no es un archivo regular. |
| 405    | Method Not Allowed    | Método distinto de `GET`. |
| 500    | Internal Server Error | Error interno (por ejemplo, no se encuentra la carpeta `www/`). |

Las rutas que terminan en `/` sirven el `index.html` de esa carpeta (`/` → `www/index.html`).

---

## Seguridad

- **Directory traversal:** se rechaza `..` en la ruta y se usa `realpath()` para que nunca se salga de `www/` (`403`).
- **Buffer overflow:** no se usa `strcpy` ni `sprintf`, solo `snprintf` con límites.
- **Métodos inválidos:** todo lo que no sea `GET` responde `405`.
- **Solicitudes muy grandes:** línea de solicitud, URI o encabezados demasiado largos responden `400`.

## Pruebas

```bash
curl -i http://127.0.0.1:8080/                               # 200
curl -i -H "Host:" http://127.0.0.1:8080/                    # 400
curl -i --path-as-is http://127.0.0.1:8080/../../etc/passwd  # 403
curl -i http://127.0.0.1:8080/no-existe.html                 # 404
curl -i -X POST http://127.0.0.1:8080/                       # 405
```

---

## Autor

Jorge Yanez - Proyecto Integrador MiniHTTPd
