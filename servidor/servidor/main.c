//
//  main.c
//  servidor
//
//  Created by Esteban Vargas Mora on 30/3/15.
//  Copyright (c) 2015 Esteban Vargas Mora. All rights reserved.
//

#include <stdio.h>       /* standard I/O routines                     */
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>     /* pthread functions and data structures     */
#include <unistd.h>      /* sleep()                                   */
#include <stdlib.h>      /* rand() and srand() functions              */

#define PORT "8080"  // the port users will be connecting to

#define BACKLOG 10     // how many pending connections queue will hold

/* number of threads used to service requests */
#define NUM_HANDLER_THREADS 3

/* global mutex for our program. assignment initializes it. */
/* note that we use a RECURSIVE mutex, since a handler      */
/* thread might try to lock it twice consecutively.         */
pthread_mutex_t request_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;

/* global condition variable for our program. assignment initializes it. */
pthread_cond_t  got_request = PTHREAD_COND_INITIALIZER;

int num_requests = 0;	/* number of pending requests, initially none */

struct s_datos_request {
    int fileDescriptor;
    char* metodo;
    char* direccion;
};
typedef struct s_datos_request* t_datos_request;

/* format of a single request. */
struct s_request {
    void* valor;		    /* number of the request                  */
    struct s_request* next;   /* pointer to next request, NULL if none. */
};
typedef struct s_request* t_request;

t_request requests = NULL;     /* head of linked list of requests. */
t_request last_request = NULL; /* pointer to last request.         */

/*
 * function add_request(): add a request to the requests list
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     request number, linked list mutex.
 * output:    none.
 */
void add_request(void* valor, pthread_mutex_t* p_mutex, pthread_cond_t*  p_cond_var) {
    
    int rc;	                    /* return code of pthreads functions.  */
    t_request a_request;        /* pointer to newly added request.     */
    
    /* create structure with new request */
    a_request = (t_request)malloc(sizeof(struct s_request));
    if (!a_request) { /* malloc failed? */
        fprintf(stderr, "add_request: out of memory\n");
        exit(1);
    }
    a_request->valor = valor;
    a_request->next = NULL;
    
    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(p_mutex);
    
    /* add new request to the end of the list, updating list */
    /* pointers as required */
    if (num_requests == 0) { /* special case - list is empty */
        requests = a_request;
        last_request = a_request;
    }
    else {
        last_request->next = a_request;
        last_request = a_request;
    }
    
    /* increase total number of pending requests by one. */
    num_requests++;
    
    //#ifdef DEBUG
    //    printf("add_request: added request with id '%s'\n", a_request->valor);
    //    fflush(stdout);
    //#endif /* DEBUG */
    
    /* unlock mutex */
    rc = pthread_mutex_unlock(p_mutex);
    
    /* signal the condition variable - there's a new request to handle */
    rc = pthread_cond_signal(p_cond_var);
}

/*
 * function get_request(): gets the first pending request from the requests list
 *                         removing it from the list.
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     request number, linked list mutex.
 * output:    pointer to the removed request, or NULL if none.
 * memory:    the returned request need to be freed by the caller.
 */
t_request get_request(pthread_mutex_t* p_mutex) {
    
    int rc;	                    /* return code of pthreads functions.  */
    t_request a_request;        /* pointer to request.                 */
    
    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(p_mutex);
    
    if (num_requests > 0) {
        a_request = requests;
        requests = a_request->next;
        if (requests == NULL) { /* this was the last request on the list */
            last_request = NULL;
        }
        /* decrease the total number of pending requests */
        num_requests--;
    }
    else { /* requests list is empty */
        a_request = NULL;
    }
    
    /* unlock mutex */
    rc = pthread_mutex_unlock(p_mutex);
    
    /* return the request to the caller. */
    return a_request;
}

/*
 * function handle_request(): handle a single given request.
 * algorithm: prints a message stating that the given thread handled
 *            the given request.
 * input:     request pointer, id of calling thread.
 * output:    none.
 */
void handle_request(t_request a_request, int thread_id) {
    
    if (a_request) {
        t_datos_request data = (t_datos_request)a_request->valor;
//        printf("Dirreción: %s\n", data->direccion);
//        printf("Metodo: %s\n", data->metodo);
//        printf("Thread '%d' File descriptor '%d'\n", thread_id, data->fileDescriptor);
//        fflush(stdout);
        send(data->fileDescriptor, "Esperemos que todo bien\n", 24, 0);
        close(data->fileDescriptor);
    }
}

/*
 * function handle_requests_loop(): infinite loop of requests handling
 * algorithm: forever, if there are requests to handle, take the first
 *            and handle it. Then wait on the given condition variable,
 *            and when it is signaled, re-do the loop.
 *            increases number of pending requests by one.
 * input:     id of thread, for printing purposes.
 * output:    none.
 */
void* handle_requests_loop(void* data) {
    
    int rc;                         /* return code of pthreads functions.  */
    t_request a_request;            /* pointer to a request.               */
    int thread_id = *((int*)data);  /* thread identifying number           */
    
    //#ifdef DEBUG
    //    printf("Starting thread '%d'\n", thread_id);
    //    fflush(stdout);
    //#endif /* DEBUG */
    
    /* lock the mutex, to access the requests list exclusively. */
    rc = pthread_mutex_lock(&request_mutex);
    
    //#ifdef DEBUG
    //    printf("thread '%d' after pthread_mutex_lock\n", thread_id);
    //    fflush(stdout);
    //#endif /* DEBUG */
    
    /* do forever.... */
    while (1) {
        //#ifdef DEBUG
        //        printf("thread '%d', num_requests =  %d\n", thread_id, num_requests);
        //        fflush(stdout);
        //#endif /* DEBUG */
        if (num_requests > 0) { /* a request is pending */
            a_request = get_request(&request_mutex);
            if (a_request) { /* got a request - handle it and free it */
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
            /* wait for a request to arrive. note the mutex will be */
            /* unlocked here, thus allowing other threads access to */
            /* requests list.                                       */
            //#ifdef DEBUG
            //            printf("thread '%d' before pthread_cond_wait\n", thread_id);
            //            fflush(stdout);
            //#endif /* DEBUG */
            rc = pthread_cond_wait(&got_request, &request_mutex);
            /* and after we return from pthread_cond_wait, the mutex  */
            /* is locked again, so we don't need to lock it ourselves */
            //#ifdef DEBUG
            //            printf("thread '%d' after pthread_cond_wait\n", thread_id);
            //            fflush(stdout);
            //#endif /* DEBUG */
        }
    }
}

void sigchld_handler(int s) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void) {
    
    int        i;                                /* loop counter          */
    int        thr_id[NUM_HANDLER_THREADS];      /* thread IDs            */
    pthread_t  p_threads[NUM_HANDLER_THREADS];   /* thread's structures   */
    
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
    hints.ai_flags = AI_PASSIVE; // use my IP
    
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
    
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    /* create the request-handling threads */
    for (i = 0; i < NUM_HANDLER_THREADS; i++) {
        thr_id[i] = i;
        pthread_create(&p_threads[i], NULL, handle_requests_loop, (void*)&thr_id[i]);
    }
    
    printf("server: waiting for connections...\n");
    
    while(1) {  // main accept() loop
        
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
        
        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("server: got connection from %s\n", s);
        
        recv(new_fd, buffer, sizeof(buffer)-1, 0);
        
        //# parsea la primer linea del request #
        char method[10];
        char url[50];
        buffer[60] = '\0';
        sscanf(buffer, "%s %s", method, url);
        
        //
         char* direccion_root = "/Users/Esteban/wwwroot";
         //
        
        char* first = direccion_root;
        char* second = url;
        char* both = malloc(strlen(first) + strlen(second) + 2);
        
        strcpy(both, first);
        strcat(both, url);
        
        //# crea el struct t_datos_request #
        t_datos_request temp = (t_datos_request) malloc(sizeof(struct s_datos_request));
        temp->fileDescriptor = new_fd;
        temp->metodo = method;
        temp->direccion = both;
        
        free(both);
        
        add_request(temp, &request_mutex, &got_request);
    }
    return 0;
}