#ifndef TABLERO_H
#define TABLERO_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include "config.h"

/* Cómo se identifica lo que hay en cada casilla. Cada Perro tiene su propio número. */
#define OCUP_VACIA (-1)
#define OCUP_PUMA  0

/* Límites del tablero y cantidad de capturas necesarias para que gane el Puma. */
#define MAX_VECINOS 4
#define MAX_JUGADAS 8
#define CAPTURAS_VICTORIA 6

/* Guarda una jugada: desde dónde sale, por dónde salta y a dónde llega. Si 'sobre' vale -1, es un movimiento normal, sin captura. */
typedef struct {
    int origen;
    int sobre;
    int destino;
} Jugada;

/* Indica si un movimiento se pudo hacer o por qué fue rechazado. */
typedef enum {
    MOV_OK = 0,          /* el movimiento se realizó */
    MOV_ILEGAL,          /* no es un paso o salto permitido */
    MOV_ORIGEN_PERDIDO,  /* la ficha ya no está ahí, por ejemplo, porque la capturaron */
    MOV_DESTINO_OCUPADO, /* otra ficha ocupa el lugar al que quería ir */
    MOV_SIN_PERRO,       /* no hay un Perro para capturar durante el salto */
    MOV_CERRADO          /* la partida terminó, así que ya no se puede mover */
} ResMov;

/* Indica si la revisión del tablero encontró un ganador. */
typedef enum {
    VIC_NINGUNA = 0,
    VIC_PUMA,            /* el puma consiguió las capturas necesarias */
    VIC_PERROS           /* el puma ya no puede moverse */
} Victoria;

/* Guarda el estado de una partida. Los mutex protegen las casillas durante
 * los movimientos y las fotos, para que los hilos no se interfieran. */
typedef struct {
    int             ocupante[NUM_CASILLAS]; /* qué ficha está en cada casilla */
    pthread_mutex_t mtx[NUM_CASILLAS];      /* un mutex para cada casilla */
    int             capturas;               /* perros capturados por el puma */
    int             n_perros;               /* cantidad inicial de perros */
    bool            cerrado;                /* true cuando ya terminó la partida */
    atomic_int      movimientos;            /* total de movimientos exitosos, sin perder incrementos */
} Tablero;

/* Prepara el tablero y sus mutex usando las posiciones iniciales del config. */
int  tablero_iniciar(Tablero *t, const Celda disposicion[NUM_CASILLAS]);

/* Libera los mutex cuando ningún hilo está usando el tablero. */
void tablero_destruir(Tablero *t);

/* Intenta mover una ficha a una casilla vecina. Bloquea origen y destino para comprobar que el movimiento todavía se pueda realizar. */
ResMov tablero_mover(Tablero *t, int id, int origen, int destino);

/* Intenta que el Puma salte sobre un perro y lo capture. Si lo consigue, puede guardar el número del perro capturado en 'capturado'. */
ResMov tablero_saltar(Tablero *t, int origen, int sobre, int destino, int *capturado);

/* Saca una foto del tablero sin mezclar movimientos a medio hacer. Devuelve cuántas capturas había en ese momento. */
int tablero_foto(Tablero *t, int foto[NUM_CASILLAS]);

/* Igual que la anterior, pero también entrega el total de movimientos correspondiente a esa misma foto. */
int tablero_foto_con_movimientos(Tablero *t, int foto[NUM_CASILLAS], int *movimientos);

/* El juez saca una foto y revisa si alguien ganó. Si hay ganador, cierra el tablero antes de dejar que las fichas vuelvan a moverse. */
Victoria tablero_evaluar_y_cerrar(Tablero *t, int foto[NUM_CASILLAS], int *capturas);

/* Cierra el tablero sin buscar ganador, ya sea por tiempo o abandono. */
int tablero_cerrar(Tablero *t, int foto[NUM_CASILLAS]);

/* Entrega las casillas vecinas a una posición y cuántas encontró. */
int  tablero_vecinos(int casilla, int vecinos[MAX_VECINOS]);

/* Entrega los saltos que permite el dibujo del tablero, sin revisar si hay fichas. */
int  tablero_saltos_desde(int casilla, Jugada out[MAX_VECINOS]);

/* Busca la posición de una ficha usando su número. Devuelve -1 si no aparece. */
int  tablero_posicion(const int foto[NUM_CASILLAS], int id);

/* Busca los movimientos que puede hacer el puma según las fichas de la foto. */
int  tablero_jugadas_puma(const int foto[NUM_CASILLAS], Jugada out[MAX_JUGADAS]);

/* Comprueba si el puma se quedó sin movimientos disponibles. */
bool tablero_puma_acorralado(const int foto[NUM_CASILLAS]);

/* Revisa que haya un puma y que los perros restantes coincidan con las capturas. */
bool tablero_verificar(const Tablero *t, const int foto[NUM_CASILLAS], int capturas);

/* Funciones para mostrar el tablero en pantalla o en el log. */

/* Convierte la foto en un dibujo de texto y lo guarda en 'buf'. */
int  tablero_formatear(const int foto[NUM_CASILLAS], char *buf, size_t n);

/* Escribe el evento y el dibujo completo del tablero sin mezclar sus líneas. */
void tablero_log(int mesa_id, const char *evento, const int foto[NUM_CASILLAS]);

#endif 
