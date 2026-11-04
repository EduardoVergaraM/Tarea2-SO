#ifndef TABLERO_H
#define TABLERO_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "config.h"

/* Ocupante de una casilla. Los perros tienen id 1..NUM_PERROS */
#define OCUP_VACIA (-1)
#define OCUP_PUMA  0

#define MAX_VECINOS 4
#define MAX_JUGADAS 8
#define CAPTURAS_VICTORIA 6

/* Un paso (sobre = -1) o un salto de captura de origen sobre 'sobre' hasta destino */
typedef struct {
    int origen;
    int sobre;
    int destino;
} Jugada;

typedef enum {
    MOV_OK = 0,
    MOV_ILEGAL,           /* no es adyacente / no es un salto válido */
    MOV_ORIGEN_PERDIDO,   /* la ficha ya no está en su casilla (fue capturada) */
    MOV_DESTINO_OCUPADO,  /* otra ficha llegó primero */
    MOV_SIN_PERRO         /* el salto no tiene un perro que capturar */
} ResMov;

/* Estado compartido de una mesa. ocupante[] y capturas solo se tocan con el mutex de la casilla */
typedef struct {
    int             ocupante[NUM_CASILLAS];
    pthread_mutex_t mtx[NUM_CASILLAS];
    int             capturas;
    int             n_perros;   /* perros al inicio; no cambia */
} Tablero;

/* Crea el tablero y sus mutex desde la disposición del config. 0 = ok, -1 = error */
int  tablero_iniciar(Tablero *t, const Celda disposicion[NUM_CASILLAS]);

/* Destruye los mutex. Llamar cuando ya no haya hilos usando el tablero */
void tablero_destruir(Tablero *t);

/*
 * Movimiento de una ficha (Puma id 0, perro id 1..12) a una casilla adyacente.
 * Toma los mutex de origen y destino en orden creciente: nunca hay deadlock.
 * Antes de mover comprueba que la ficha siga en 'origen' (si no, MOV_ORIGEN_PERDIDO).
 */
ResMov tablero_mover(Tablero *t, int id, int origen, int destino);

/*
 * Salto de captura del Puma. Toma los 3 mutex en orden creciente.
 * Si captura, deja libre la casilla del perro y devuelve su id en *capturado.
 */
ResMov tablero_saltar(Tablero *t, int origen, int sobre, int destino, int *capturado);

/*
 * Copia consistente del tablero (toma las 28 casillas en orden creciente).
 * Devuelve las capturas realizadas hasta ese instante, coherentes con la foto.
 */
int tablero_foto(Tablero *t, int foto[NUM_CASILLAS]);

/* ---- Funciones puras (sin mutex): geometría y reglas sobre una foto ---- */

/* Casillas adyacentes a 'casilla'. Devuelve cuántas */
int  tablero_vecinos(int casilla, int vecinos[MAX_VECINOS]);

/* Saltos posibles desde 'casilla' según la geometría (sin mirar ocupantes) */
int  tablero_saltos_desde(int casilla, Jugada out[MAX_VECINOS]);

/* Casilla donde está la ficha con ese id, o -1 */
int  tablero_posicion(const int foto[NUM_CASILLAS], int id);

/* Movimientos válidos del Puma en esa foto. Devuelve cuántos */
int  tablero_jugadas_puma(const int foto[NUM_CASILLAS], Jugada out[MAX_JUGADAS]);

/* true si el Puma no tiene ningún movimiento válido (ganan los Perros) */
bool tablero_puma_acorralado(const int foto[NUM_CASILLAS]);

/* Invariantes: 1 Puma, ids de perros únicos y perros = n_perros - capturas */
bool tablero_verificar(const Tablero *t, const int foto[NUM_CASILLAS], int capturas);

/* ---- Log ---- */

/* Dibuja la foto en buf (7 líneas). Devuelve los bytes escritos */
int  tablero_formatear(const int foto[NUM_CASILLAS], char *buf, size_t n);

/* Escribe en el log un evento y el tablero, en un solo bloque que no se mezcla con otras mesas */
void tablero_log(int mesa_id, const char *evento, const int foto[NUM_CASILLAS]);

#endif /* TABLERO_H */
