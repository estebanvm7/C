//
//  Cola.h
//  cola
//
//  Created by Esteban Vargas Mora on 31/3/15.
//  Copyright (c) 2015 Esteban Vargas Mora. All rights reserved.
//

#include <stdio.h>

struct s_nodo{
    void* valor;
    struct s_nodo* next;
};

typedef struct s_nodo* t_nodo;

struct s_cola {
    t_nodo primero;
    t_nodo ultimo;
    unsigned largo;
};

typedef struct s_cola* t_cola;

t_cola nuevaCola();

void push(void* valor, t_cola);
void* pop(t_cola cola);
