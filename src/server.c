#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

int main(){
        int val;
        int soc1;
        int clientes[5];
        for(int j = 0; j < 5; j++){
                clientes[j] = -1;
        }
        struct epoll_event ep1 ={0};
        struct epoll_event ev[5];

        soc1 = socket(AF_INET, SOCK_STREAM, 0);
        if (soc1 == -1){
                perror("No se puedo crear el socket");
                return 1;
        }

        int opt = 1;
        if(setsockopt(soc1, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1){
                perror("Error en setsockopt");
                close(soc1);
                return 1;
        }

        int flags_soc1 = fcntl(soc1, F_GETFL, 0);
        if(flags_soc1 == -1 || fcntl(soc1, F_SETFL, flags_soc1 | O_NONBLOCK) == -1){
                perror("Error al configurar el socket de escucha como no bloqueante");
                close(soc1);
                return 1;
        }

        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(9090);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if(bind(soc1, (struct sockaddr *)&addr, sizeof(addr))==-1){
                perror("Error al asociar la direccion");
                close(soc1);
                return 1;
        }

        if(listen(soc1, 5) == -1){
                perror("Error no esta escuchando\n");
                close(soc1);
                return 1;
        }

        val = epoll_create1(0);

        if(val == -1){
                perror("Ocurrio un error\n");
                close(soc1);
                return 1;
        }

        printf("Descriptor epoll: %d\n", val);

        ep1.events = EPOLLIN;
        ep1.data.fd = soc1;

        if(epoll_ctl(val, EPOLL_CTL_ADD, soc1, &ep1) == -1){
                perror("Error al añadir el socket\n");
                close(soc1);
                close(val);
                return 1;
        }

        while(1){
                int wait = epoll_wait(val, ev, 5, -1);
                if(wait == -1){
                        perror("Error en epoll_wait");
                        break;
                }else if(wait == 0){
                        printf("No se obtuvo ningun evento\n");
                }else{
                        for(int i = 0; i < wait; i++){
                                if(ev[i].data.fd == soc1 && (ev[i].events & EPOLLIN)){
                                        printf("Evento detectado, socket listo para aceptar conexiones\n");
                                        int nuevo_cliente = accept(soc1, NULL, NULL);

                                        if(nuevo_cliente == -1){
                                                if(errno == EAGAIN || errno == EWOULDBLOCK){
                                                        // No hay más conexiones pendientes en este momento
                                                        continue;
                                                }
                                                perror("error al conectar con un cliente");
                                                continue;
                                        }
                                        printf("Descriptor cliente: %d\n", nuevo_cliente);

                                        int guardado = 0;
                                        int idx_libre = -1;
                                        for(int j = 0; j < 5; j++){
                                                if(clientes[j] == -1){
                                                        idx_libre = j;
                                                        guardado = 1;
                                                        break;
                                                }
                                        }

                                        if(!guardado){
                                                printf("No hay espacio en el arreglo de clientes, rechazando conexion\n");
                                                close(nuevo_cliente);
                                        } else {
                                                int flags = fcntl(nuevo_cliente, F_GETFL, 0);
                                                if(flags == -1 || fcntl(nuevo_cliente, F_SETFL, flags | O_NONBLOCK) == -1){
                                                        perror("Error al configurar el cliente como no bloqueante");
                                                        close(nuevo_cliente);
                                                        continue;
                                                }

                                                struct epoll_event ev_cliente = {0};
                                                ev_cliente.events = EPOLLIN;
                                                ev_cliente.data.fd = nuevo_cliente;

                                                if(epoll_ctl(val, EPOLL_CTL_ADD, nuevo_cliente, &ev_cliente) == -1){
                                                        perror("Error al registrar el cliente en epoll");
                                                        close(nuevo_cliente);
                                                        continue;
                                                }

                                                clientes[idx_libre] = nuevo_cliente;
                                                printf("Cliente asignado a la posición del arreglo: %d\n", idx_libre);
                                                printf("Registro en epoll completado\n");
                                        }
                                }else{
                                        int es_cliente = 0;
                                        for(int j = 0; j < 5; j++){
                                                if(clientes[j] != -1 && ev[i].data.fd == clientes[j] && (ev[i].events & EPOLLIN)){
                                                        es_cliente = 1;
                                                        printf("El cliente (descriptor %d) genero un evento de lectura\n", clientes[j]);
                                                        
                                                        char buff[128];
                                                        ssize_t recibir = recv(clientes[j], buff, sizeof(buff), 0);

                                                        if(recibir < 0){
                                                                if(errno == EAGAIN || errno == EWOULDBLOCK){
                                                                        printf("socket no bloqueante y no hay datos disponibles\n");
                                                                }else{
                                                                        perror("No se recibieron datos");
                                                                }
                                                        }else if(recibir == 0){
                                                                printf("Se cerro la conexion\n");
                                                                close(clientes[j]);
                                                                clientes[j] = -1;
                                                                printf("posicion de arreglo: %d\n", j);
                                                        }else{
                                                                fwrite(buff, 1, recibir, stdout);
                                                                fflush(stdout);
                                                        }
                                                        break;
                                                }
                                        }

                                        if(!es_cliente){
                                                if(ev[i].data.fd == soc1){
                                                        printf("Evento en el socket de escucha\n");
                                                }else{
                                                        printf("Se detecto otro tipo de evento en el descriptor %d\n", ev[i].data.fd);
                                                }
                                        }
                                }
                        }
                }
        }

        for(int j = 0; j < 5; j++){
                if(clientes[j] != -1){
                        close(clientes[j]);
                }
        }
        close(val);
        close(soc1);
        return 0;
}