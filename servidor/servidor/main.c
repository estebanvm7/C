//
//  main.c
//  servidor
//
//  Created by Esteban Vargas Mora on 30/3/15.
//  Copyright (c) 2015 Esteban Vargas Mora. All rights reserved.
//

#include <stdio.h>          /* entrada y salida estandar */
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>        /* hilos */
#include <unistd.h>         /* sleep() */
#include <stdlib.h>         /* rand() and srand() functions */

#define ERROR_501 "Error 501, metodo no implemantado"
#define ERROR_404 "Error 404, archivo no encontrado"
#define BUFFER_SIZE 512     /* tamaño del buffer */
#define BACKLOG 10          /* cantidad conexiones en la cola del socket */

/* mutex global */
pthread_mutex_t request_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;

/* variable de condición global */
pthread_cond_t  got_request = PTHREAD_COND_INITIALIZER;

int num_requests = 0;           /* numero de requests pendientes */
char* direccion_root;           /* dirección carpeta local */
int THREADS;                    /* numero de hilos para realizar los requests */
char* PORT;                     /* el puerto a conectar */
volatile sig_atomic_t flag = 0; /* bandera para el control de interupción */

/* struct almacena el metodo, la dirreción y el file decriptor de cada request*/
struct s_datos_request {
    int fileDescriptor;
    char* metodo;
    char* direccion;
};
typedef struct s_datos_request* t_datos_request;

/* nodo */
struct s_request {
    void* valor;                /* información del request (t_datos_request) */
    struct s_request* next;     /* puntero al proximo nodo */
};
typedef struct s_request* t_request;

t_request requests = NULL;     /* primer nodo en la cola */
t_request last_request = NULL; /* ultimo nodo en la cola */

//void my_function(int signal){ // can be called asynchronously
//    flag = 1; // set flag
//}

/*
 * función add_request(): agrega un request a la cola
 * algoritmo:   la estructura del request, lo agrega a la cola, incrementa
                el numero de request pendientes.
 * entrada:     valor (t_dato_request) , mutex.
 * salida:      ninguna.
 */
void add_request(void* valor, pthread_mutex_t* p_mutex, pthread_cond_t*  p_cond_var) {
    
    int rc;	                    /* retorna el codigo de la función pthread  */
    t_request a_request;        /* puntero al nuevo request */
    
    /* crea un request */
    a_request = (t_request)malloc(sizeof(struct s_request));
    if (!a_request) { /* fallo del malloc */
        fprintf(stderr, "add_request: memoria agotada\n");
        exit(1);
    }
    a_request->valor = valor;
    a_request->next = NULL;
    
    /* bloquea el mutex para tener acceso exlusivo */
    rc = pthread_mutex_lock(p_mutex);
    
    /* agrega el request al final de la cola */
    if (num_requests == 0) { /* caso especia, cola vacía */
        requests = a_request;
        last_request = a_request;
    }
    else {
        last_request->next = a_request;
        last_request = a_request;
    }
    
    /* incrementa el numero de request pendientes */
    num_requests++;
    
    /* desbloquea el mutex */
    rc = pthread_mutex_unlock(p_mutex);
    
    /* cambia la variable de condición - nuevo request para procesar */
    rc = pthread_cond_signal(p_cond_var);
}

/*
 * función get_request(): obtiene el primer request disponible.
 * algoritmo:   saca un request de la cola.
 * entrada:     mutex.
 * salida:      puntero al request.
 */
t_request get_request(pthread_mutex_t* p_mutex) {
    
    int rc;	                    /* retorna el codigo de la función pthread  */
    t_request a_request;        /* puntero al nuevo request */
    
    /* bloquea el mutex para tener acceso exlusivo */
    rc = pthread_mutex_lock(p_mutex);
    
    if (num_requests > 0) {
        a_request = requests;
        requests = a_request->next;
        if (requests == NULL) {
            last_request = NULL;
        }
        /* decrementa los request pendientes */
        num_requests--;
    }
    else { /* cola vacía */
        a_request = NULL;
    }
    
    /* desbloquea mutex */
    rc = pthread_mutex_unlock(p_mutex);
    
    /* returna el request */
    return a_request;
}

/*
 * función process_file(): procesa el archivo dado.
 * algorithm: lee y envía el archivo.
 * entrada:     archivo.
 * salida:    none.
 */
void process_file(FILE* p_file, int RFD) {
    char buffer[BUFFER_SIZE];
    size_t count;
    
    while((count = fread(buffer, sizeof(char), BUFFER_SIZE - 1, p_file))) {
        buffer[count] = '\0';
        send(RFD, buffer, sizeof(buffer)-1, 0);
        bzero(buffer, sizeof(buffer));
    }
}


/*
 * función handle_request(): ejecuta el request
 * algorithm:   verifica el metodo del request, concatena la dirección local
                con el pedido en el request, llama a la función para procesar
                el archivo
 * entrada:     puntero a un request, id del hilo.
 * salida:      ninguna.
 */
void handle_request(t_request a_request, int thread_id) {
    
    if (a_request) {
        t_datos_request data = (t_datos_request)a_request->valor;
        
        /* verifica que sea por el metodo GET */
        if (strncmp(data->metodo, "GET", 3) == 0) {
            
            if (strcmp(data->direccion, "/") == 0) { /* si no se pide nada */
                send(data->fileDescriptor, ERROR_404, sizeof(ERROR_404)-1, 0);
            }
            else {
                /* concatena la dirrección del request con la ubicación de los archivos */
                char* local = direccion_root;
                char* both = malloc(strlen(local) + strlen(data->direccion) + 2);
                strcpy(both, local);
                strcat(both, data->direccion);

                /* busca y abre el archivo */
                FILE* file = fopen(both, "r");
                free(both);/* libera la memoria */
                if (file != NULL) {
                    process_file(file, data->fileDescriptor);
                }
                else {
                    /* despliega error 404 en caso de no existir el archivo */
                    send(data->fileDescriptor, ERROR_404, sizeof(ERROR_404)-1, 0);
                }
                /* cierra el archivo */
                fclose(file);
            }
        }
        else {
            /* envía error de metodo incorrecto */
            send(data->fileDescriptor, ERROR_501, sizeof(ERROR_501)-1, 0);
        }
        /* cierra la conexión */
        close(data->fileDescriptor);
    }
}

/*
 * función handle_requests_loop(): loop que llama a ejecutar los request
 * algoritmo:   infinito, toma el primer request.
                espea a la variable de condición para repetir el proceso.
                libera la memoria del request ejecutado.
 * entrada:
 * salida:      ninguna.
 */
void* handle_requests_loop(void* data) {
    
    int rc;                         /* retorna el codigo de la función pthread  */
    t_request a_request;            /* puntero al nuevo request */
    int thread_id = *((int*)data);  /* id hilo */
    
    /* bloquea el mutex para tener acceso exlusivo */
    rc = pthread_mutex_lock(&request_mutex);
    
    while (1) {

        if (num_requests > 0) { /* si hay requests pendientes */
            a_request = get_request(&request_mutex);
            if (a_request) {
                handle_request(a_request, thread_id);
                
                /* libera la memoria dentro del request (t_datos_request) */
                t_datos_request temp = (t_datos_request)a_request->valor;
                free(temp);
                temp = NULL;
                
                /* libera la memoria del request */
                free(a_request);
                a_request = NULL;
            }
        }
        else {
            /* wait for a request to arrive. desbloquea el mutex */
            rc = pthread_cond_wait(&got_request, &request_mutex);
            /* bloquea el mutex cunado termina de ejecutar*/
        }
    }
}

void sigchld_handler(int s) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

// obtiene sockaddr, IPv4 o IPv6:
void *get_in_addr(struct sockaddr *sa) {
    
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
 * función main(int argc, char * argv[]): ejecuta el socket
 * entrada:     -R  dirección local de los archivos
                -W  cantidad de hilos a utilizar
                -P  puerto a conectar el socket
 * salida:      ninguna.
 */
int main(int argc, char * argv[]) {

    for (int i = 1; i < argc; i += 2) {
        char* opcion = argv[i];
        if (strncmp(opcion, "-R", 2) == 0) {
            direccion_root = argv[i + 1];
            printf("Dirección: %s\n", direccion_root);
        }
        else if (strncmp(opcion, "-W", 2) == 0) {
            THREADS = atoi(argv[i + 1]);
            printf("Hilos: %d\n", THREADS);
        }
        else if (strncmp(opcion, "-P", 2) == 0) {
            PORT = argv[i + 1];
            printf("Puerto: %s\n", PORT);
        } else { ;}
    }
    
    int        i;
    int        thr_id[THREADS];      /* arreglo de los ids de los hilos */
    pthread_t  p_threads[THREADS];   /* estructura del hilo */
    
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    
    char buffer[256];
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }
    
    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }
    
    freeaddrinfo(servinfo); // all done with this structure
    
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
    
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    /* crea los hilos */
    for (i = 0; i < THREADS; i++) {
        thr_id[i] = i;
        pthread_create(&p_threads[i], NULL, handle_requests_loop, (void*)&thr_id[i]);
    }
    
    printf("server: waiting for connections...\n");
    
//    signal(SIGINT, my_function);
    
    while(1) {  /* accept() loop */
        
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
        
//        if(flag){ // my action when signal set it 1
//            
//            t_request a_request;            /* puntero a un request */
//            
//            while (requests != NULL) {
//                a_request = requests;
//                
//                t_datos_request data = (t_datos_request)a_request->valor;
//                close(data->fileDescriptor);
//                
//                /* libera la memoria dentro del request (t_datos_request) */
//                t_datos_request temp = (t_datos_request)a_request->valor;
//                free(temp);
//                temp = NULL;
//                
//                /* libera la memoria del request */
//                free(a_request);
//                a_request = NULL;
//                
//                requests = a_request->next;
//                if (requests == NULL) { /* this was the last request on the list */
//                    printf("exit ultimo elemento");
//                    exit(0);
//                }
//                /* decrease the total number of pending requests */
//                num_requests--;
//                printf("elemento liberado");
//            }
//            printf("exit no hay elementos");
//            exit(0); /* si no hay nada que liberar */
//        }
        
        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("server: got connection from %s\n", s);
        
        recv(new_fd, buffer, sizeof(buffer)-1, 0);
        
        /* parsea la primer linea del request */
        char method[10];
        char url[50];
        buffer[60] = '\0';
        sscanf(buffer, "%s %s", method, url);
        
        /* crea el struct t_datos_request */
        t_datos_request temp = (t_datos_request) malloc(sizeof(struct s_datos_request));
        temp->fileDescriptor = new_fd;
        temp->metodo = method;
        temp->direccion = url;
        
        add_request(temp, &request_mutex, &got_request);
    }
    return 0;
}