//
//  Cola.c
//  cola
//
//  Created by Esteban Vargas Mora on 31/3/15.
//  Copyright (c) 2015 Esteban Vargas Mora. All rights reserved.
//

#include "Cola.h"
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>

pthread_mutex_t a_mutex = PTHREAD_MUTEX_INITIALIZER;

t_cola nuevaCola(){
    
    t_cola cola = (t_cola) malloc(sizeof(struct s_cola));
    cola->primero = 0;
    cola->ultimo = 0;
    cola->largo = 0;
    
    return cola;
}


void push(void* valor, t_cola cola){
    
    pthread_mutex_lock(&a_mutex);
    
    if (cola->primero == 0) {
        t_nodo nodo = (t_nodo) malloc(sizeof(struct s_nodo));
        nodo->valor = valor;
        nodo->next = 0;
        
        cola->primero = nodo;
        cola->ultimo = nodo;
        cola->largo++;
    } else {
        t_nodo nodo = (t_nodo) malloc(sizeof(struct s_nodo));
        nodo->valor = valor;
        nodo->next = 0;
        
        cola->ultimo->next = nodo; //el siguente lo apunta al nuevo nodo
        cola->ultimo = nodo; //el nuevo nodo es el ultimo
        cola->largo++;
    }
    
    pthread_mutex_unlock(&a_mutex);
}

void* pop(t_cola cola){
    
    pthread_mutex_lock(&a_mutex);
    
    //Caso con cola vacía//
    if (cola->primero == 0) {
        printf("%s", "Cola vacía");
        return 0; //no hay valores disponibles
    }
    t_nodo temp = cola->primero;
    void* valorNodo = temp->valor;
    
    //Reasigna el valor del primero//
    cola->primero = cola->primero->next;
    cola->largo--; //decrementa el largo
    
    //libera la memoria//
    temp->next = 0;
    free(temp);
    
    pthread_mutex_unlock(&a_mutex);
    
    return valorNodo;
}