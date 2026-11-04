/* tablero.c: Implementación de la lógica y sincronización del tablero de Komikan */
#define _POSIX_C_SOURCE 200809L
#include "tablero.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* --- Inicialización y Destrucción --- */

int tablero_iniciar(Tablero *t, const Celda disposicion[NUM_CASILLAS]) {
    int id_perro = 1;
    t->capturas = 0;
    t->n_perros = 0;

    for (int i = 0; i < NUM_CASILLAS; i++) {
        /* Inicializa el mutex de cada casilla individual */
        if (pthread_mutex_init(&t->mtx[i], NULL) != 0) {
            return -1;
        }

        if (disposicion[i] == CELDA_PUMA) {
            t->ocupante[i] = OCUP_PUMA;
        } else if (disposicion[i] == CELDA_PERRO) {
            t->ocupante[i] = id_perro++;
            t->n_perros++;
        } else {
            t->ocupante[i] = OCUP_VACIA;
        }
    }
    return 0;
}

void tablero_destruir(Tablero *t) {
    for (int i = 0; i < NUM_CASILLAS; i++) {
        pthread_mutex_destroy(&t->mtx[i]);
    }
}

/* --- Funciones de Sincronización y Movimiento --- */

/* Utilidad interna para ordenar 2 números (para los mutex) */
static void ordenar_dos(int *a, int *b) {
    if (*a > *b) { int tmp = *a; *a = *b; *b = tmp; }
}

/* Utilidad interna para ordenar 3 números (para los mutex del salto) */
static void ordenar_tres(int *a, int *b, int *c) {
    ordenar_dos(a, b);
    ordenar_dos(b, c);
    ordenar_dos(a, b);
}

ResMov tablero_mover(Tablero *t, int id, int origen, int destino) {
    int m1 = origen, m2 = destino;
    ordenar_dos(&m1, &m2); // Orden creciente para evitar deadlock

    pthread_mutex_lock(&t->mtx[m1]);
    pthread_mutex_lock(&t->mtx[m2]);

    ResMov res = MOV_OK;

    if (t->ocupante[origen] != id) {
        res = MOV_ORIGEN_PERDIDO; /* La ficha fue capturada antes de moverse */
    } else if (t->ocupante[destino] != OCUP_VACIA) {
        res = MOV_DESTINO_OCUPADO; /* Otro hilo llegó primero */
    } else {
        /* Movimiento exitoso */
        t->ocupante[destino] = id;
        t->ocupante[origen] = OCUP_VACIA;
    }

    pthread_mutex_unlock(&t->mtx[m2]);
    pthread_mutex_unlock(&t->mtx[m1]);

    return res;
}

ResMov tablero_saltar(Tablero *t, int origen, int sobre, int destino, int *capturado) {
    int m1 = origen, m2 = sobre, m3 = destino;
    ordenar_tres(&m1, &m2, &m3); // Orden creciente estricto para evitar deadlock

    pthread_mutex_lock(&t->mtx[m1]);
    pthread_mutex_lock(&t->mtx[m2]);
    pthread_mutex_lock(&t->mtx[m3]);

    ResMov res = MOV_OK;

    if (t->ocupante[origen] != OCUP_PUMA) {
        res = MOV_ORIGEN_PERDIDO;
    } else if (t->ocupante[destino] != OCUP_VACIA) {
        res = MOV_DESTINO_OCUPADO;
    } else if (t->ocupante[sobre] <= 0) {
        res = MOV_SIN_PERRO; /* No hay un perro válido para capturar */
    } else {
        /* Captura exitosa */
        *capturado = t->ocupante[sobre];
        t->ocupante[destino] = OCUP_PUMA;
        t->ocupante[sobre] = OCUP_VACIA;
        t->ocupante[origen] = OCUP_VACIA;
        t->capturas++;
    }

    pthread_mutex_unlock(&t->mtx[m3]);
    pthread_mutex_unlock(&t->mtx[m2]);
    pthread_mutex_unlock(&t->mtx[m1]);

    return res;
}

int tablero_foto(Tablero *t, int foto[NUM_CASILLAS]) {
    /* Toma los 28 mutex en orden creciente para una copia consistente */
    for (int i = 0; i < NUM_CASILLAS; i++) {
        pthread_mutex_lock(&t->mtx[i]);
    }

    for (int i = 0; i < NUM_CASILLAS; i++) {
        foto[i] = t->ocupante[i];
    }
    int caps = t->capturas;

    /* Libera los 28 mutex */
    for (int i = 0; i < NUM_CASILLAS; i++) {
        pthread_mutex_unlock(&t->mtx[i]);
    }

    return caps;
}

/* --- Geometría y Funciones Puras (sin Mutex) --- */

int tablero_vecinos(int casilla, int vecinos[MAX_VECINOS]) {
    int count = 0;
    
    /* 
     * AQUÍ DEBES DEFINIR LA GEOMETRÍA EXACTA.
     * Ejemplo básico (ajusta según las líneas exactas de la imagen del PDF):
     * 0 = trono, 1 = triángulo izq, 2 = triángulo der.
     * Grilla 5x5: índices del 3 al 27.
     */
    if (casilla == 0) { vecinos[count++] = 1; vecinos[count++] = 2; vecinos[count++] = 5; }
    else if (casilla == 1) { vecinos[count++] = 0; vecinos[count++] = 2; vecinos[count++] = 3; vecinos[count++] = 5; }
    else if (casilla == 2) { vecinos[count++] = 0; vecinos[count++] = 1; vecinos[count++] = 5; vecinos[count++] = 7; }
    else {
        // Lógica de cuadrícula 5x5 genérica (movimientos ortogonales)
        int fila = (casilla - 3) / 5;
        int col = (casilla - 3) % 5;
        
        if (fila > 0) vecinos[count++] = casilla - 5; // Arriba
        if (fila < 4) vecinos[count++] = casilla + 5; // Abajo
        if (col > 0) vecinos[count++] = casilla - 1;  // Izquierda
        if (col < 4) vecinos[count++] = casilla + 1;  // Derecha
        
        // (Nota: Si el tablero Komikan tiene movimientos diagonales válidos en tu versión,
        // deberás agregarlos aquí dependiendo de si la suma de (fila+col) es par o impar).
    }
    return count;
}

int tablero_saltos_desde(int casilla, Jugada out[MAX_JUGADAS]) {
    int count = 0;
    /* 
     * Implementa la lógica de qué casilla se salta ("sobre") para llegar al "destino".
     * Ejemplo para saltar 2 casillas hacia la derecha en la misma fila:
     */
    if (casilla >= 3) {
        int fila = (casilla - 3) / 5;
        int col = (casilla - 3) % 5;
        
        if (col <= 2) { out[count++] = (Jugada){casilla, casilla + 1, casilla + 2}; } // Der
        if (col >= 2) { out[count++] = (Jugada){casilla, casilla - 1, casilla - 2}; } // Izq
        if (fila <= 2) { out[count++] = (Jugada){casilla, casilla + 5, casilla + 10}; } // Abajo
        if (fila >= 2) { out[count++] = (Jugada){casilla, casilla - 5, casilla - 10}; } // Arriba
    }
    return count;
}

int tablero_posicion(const int foto[NUM_CASILLAS], int id) {
    for (int i = 0; i < NUM_CASILLAS; i++) {
        if (foto[i] == id) return i;
    }
    return -1;
}

int tablero_jugadas_puma(const int foto[NUM_CASILLAS], Jugada out[MAX_JUGADAS]) {
    int pos_puma = tablero_posicion(foto, OCUP_PUMA);
    if (pos_puma == -1) return 0;
    
    int count = 0;
    
    /* 1. Buscar movimientos normales */
    int vecinos[MAX_VECINOS];
    int n_vecinos = tablero_vecinos(pos_puma, vecinos);
    for (int i = 0; i < n_vecinos; i++) {
        if (foto[vecinos[i]] == OCUP_VACIA) {
            out[count++] = (Jugada){pos_puma, -1, vecinos[i]};
        }
    }
    
    /* 2. Buscar saltos de captura */
    Jugada posibles_saltos[MAX_JUGADAS];
    int n_saltos = tablero_saltos_desde(pos_puma, posibles_saltos);
    for (int i = 0; i < n_saltos; i++) {
        if (foto[posibles_saltos[i].sobre] > 0 && foto[posibles_saltos[i].destino] == OCUP_VACIA) {
            out[count++] = posibles_saltos[i];
        }
    }
    
    return count;
}

bool tablero_puma_acorralado(const int foto[NUM_CASILLAS]) {
    Jugada jugadas[MAX_JUGADAS];
    return tablero_jugadas_puma(foto, jugadas) == 0;
}

bool tablero_verificar(const Tablero *t, const int foto[NUM_CASILLAS], int capturas) {
    int pumas = 0, perros = 0;
    for (int i = 0; i < NUM_CASILLAS; i++) {
        if (foto[i] == OCUP_PUMA) pumas++;
        else if (foto[i] > 0) perros++;
    }
    return (pumas == 1 && perros == (t->n_perros - capturas));
}

/* --- Logs e Impresión --- */

int tablero_formatear(const int foto[NUM_CASILLAS], char *buf, size_t n) {
    char simbolos[NUM_CASILLAS];
    for (int i = 0; i < NUM_CASILLAS; i++) {
        if (foto[i] == OCUP_PUMA) simbolos[i] = 'P';
        else if (foto[i] > 0) simbolos[i] = 'D';
        else simbolos[i] = '.';
    }
    
    return snprintf(buf, n,
        "    %c\n"
        "   %c %c\n"
        "%c %c %c %c %c\n"
        "%c %c %c %c %c\n"
        "%c %c %c %c %c\n"
        "%c %c %c %c %c\n"
        "%c %c %c %c %c\n",
        simbolos[0],
        simbolos[1], simbolos[2],
        simbolos[3], simbolos[4], simbolos[5], simbolos[6], simbolos[7],
        simbolos[8], simbolos[9], simbolos[10], simbolos[11], simbolos[12],
        simbolos[13], simbolos[14], simbolos[15], simbolos[16], simbolos[17],
        simbolos[18], simbolos[19], simbolos[20], simbolos[21], simbolos[22],
        simbolos[23], simbolos[24], simbolos[25], simbolos[26], simbolos[27]
    );
}

void tablero_log(int mesa_id, const char *evento, const int foto[NUM_CASILLAS]) {
    char buf[512];
    tablero_formatear(foto, buf, sizeof(buf));
    
    /* Imprime el evento y el tablero en un solo bloque atómico */
    log_evento("[mesa %d] %s\n%s", mesa_id, evento, buf);
}