/* tablero.c: geometría del Komikan, reglas de movimiento y exclusión mutua por casilla */
#define _POSIX_C_SOURCE 200809L
#include "tablero.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

_Static_assert(NUM_CASILLAS == 28, "la geometría está escrita para 28 casillas");

/*
 * Numeración (igual que config.h): 0 = trono, 1-2 = triángulo, 3 + 5*fila + col = grilla.
 * Posición de cada casilla en el plano de la figura. Sirve para saber qué tres casillas
 * quedan en línea recta (un salto solo es válido a lo largo de una línea del tablero).
 */
static int px[NUM_CASILLAS], py[NUM_CASILLAS];

static int     vecino[NUM_CASILLAS][MAX_VECINOS];
static int     n_vecinos[NUM_CASILLAS];
static Jugada  salto[NUM_CASILLAS][MAX_VECINOS];
static int     n_saltos[NUM_CASILLAS];
static pthread_once_t tablas_listas = PTHREAD_ONCE_INIT;

static void unir(int a, int b)
{
    vecino[a][n_vecinos[a]++] = b;
    vecino[b][n_vecinos[b]++] = a;
}

/* Tablas de adyacencia y de saltos. Se arman una sola vez y después son de solo lectura */
static void construir_tablas(void)
{
    px[0] = 4; py[0] = 0;                      /* trono */
    px[1] = 2; py[1] = 1;                      /* triángulo izquierdo */
    px[2] = 6; py[2] = 1;                      /* triángulo derecho */
    for (int i = 3; i < NUM_CASILLAS; i++) {
        px[i] = 2 * ((i - 3) % 5);
        py[i] = 2 + (i - 3) / 5;
    }

    /* Grilla 5x5: solo líneas horizontales y verticales */
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 5; c++) {
            int i = 3 + 5 * r + c;
            if (c < 4) unir(i, i + 1);
            if (r < 4) unir(i, i + 5);
        }

    /* Trono y triángulo (la línea 0-5 cruza la barra del triángulo sin casilla en medio) */
    unir(0, 1); unir(0, 2); unir(0, 5);
    unir(1, 2); unir(1, 3); unir(1, 4);
    unir(2, 6); unir(2, 7);

    /* Salto a -> c sobre b: a y c vecinos de b, en la misma línea y sentidos opuestos */
    for (int b = 0; b < NUM_CASILLAS; b++)
        for (int i = 0; i < n_vecinos[b]; i++)
            for (int j = 0; j < n_vecinos[b]; j++) {
                int a, c, ax, ay, cx, cy;

                if (i == j)
                    continue;
                a = vecino[b][i];
                c = vecino[b][j];
                ax = px[b] - px[a];
                ay = py[b] - py[a];
                cx = px[c] - px[b];
                cy = py[c] - py[b];
                if (ax * cy - ay * cx == 0 && ax * cx + ay * cy > 0 && n_saltos[a] < MAX_VECINOS) {
                    salto[a][n_saltos[a]].origen = a;
                    salto[a][n_saltos[a]].sobre = b;
                    salto[a][n_saltos[a]].destino = c;
                    n_saltos[a]++;
                }
            }
}

static bool casilla_valida(int c)
{
    return c >= 0 && c < NUM_CASILLAS;
}

static bool es_vecino(int a, int b)
{
    for (int i = 0; i < n_vecinos[a]; i++)
        if (vecino[a][i] == b)
            return true;
    return false;
}

/* ---- Bloqueo ordenado: siempre de menor a mayor índice ---- */

static void ordenar(int *v, int n)
{
    for (int i = 1; i < n; i++) {
        int x = v[i], j = i - 1;
        while (j >= 0 && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
}

static void bloquear(Tablero *t, int *cs, int n)
{
    ordenar(cs, n);
    for (int i = 0; i < n; i++)
        pthread_mutex_lock(&t->mtx[cs[i]]);
}

static void liberar(Tablero *t, const int *cs, int n)
{
    for (int i = n - 1; i >= 0; i--)
        pthread_mutex_unlock(&t->mtx[cs[i]]);
}

/* ---- Ciclo de vida ---- */

int tablero_iniciar(Tablero *t, const Celda disposicion[NUM_CASILLAS])
{
    int pumas = 0, perros = 0, creados = 0;

    pthread_once(&tablas_listas, construir_tablas);
    memset(t, 0, sizeof *t);

    for (int i = 0; i < NUM_CASILLAS; i++) {
        switch (disposicion[i]) {
        case CELDA_PUMA:  t->ocupante[i] = OCUP_PUMA; pumas++; break;
        case CELDA_PERRO: t->ocupante[i] = ++perros; break;
        default:          t->ocupante[i] = OCUP_VACIA; break;
        }
    }
    if (pumas != 1 || perros > NUM_PERROS)
        return -1;
    t->n_perros = perros;

    for (; creados < NUM_CASILLAS; creados++)
        if (pthread_mutex_init(&t->mtx[creados], NULL) != 0)
            break;
    if (creados < NUM_CASILLAS) {
        while (creados-- > 0)
            pthread_mutex_destroy(&t->mtx[creados]);
        return -1;
    }
    return 0;
}

void tablero_destruir(Tablero *t)
{
    for (int i = 0; i < NUM_CASILLAS; i++)
        pthread_mutex_destroy(&t->mtx[i]);
}

/* ---- Movimientos concurrentes ---- */

ResMov tablero_mover(Tablero *t, int id, int origen, int destino)
{
    int cs[2] = { origen, destino };
    ResMov r;

    if (!casilla_valida(origen) || !casilla_valida(destino) || !es_vecino(origen, destino))
        return MOV_ILEGAL;

    bloquear(t, cs, 2);
    if (t->ocupante[origen] != id)
        r = MOV_ORIGEN_PERDIDO;
    else if (t->ocupante[destino] != OCUP_VACIA)
        r = MOV_DESTINO_OCUPADO;
    else {
        t->ocupante[destino] = id;
        t->ocupante[origen] = OCUP_VACIA;
        r = MOV_OK;
    }
    liberar(t, cs, 2);
    return r;
}

ResMov tablero_saltar(Tablero *t, int origen, int sobre, int destino, int *capturado)
{
    int cs[3] = { origen, sobre, destino };
    bool valido = false;
    ResMov r;

    if (!casilla_valida(origen) || !casilla_valida(sobre) || !casilla_valida(destino))
        return MOV_ILEGAL;
    for (int i = 0; i < n_saltos[origen]; i++)
        if (salto[origen][i].sobre == sobre && salto[origen][i].destino == destino)
            valido = true;
    if (!valido)
        return MOV_ILEGAL;

    bloquear(t, cs, 3);
    if (t->ocupante[origen] != OCUP_PUMA)
        r = MOV_ORIGEN_PERDIDO;
    else if (t->ocupante[sobre] < 1)
        r = MOV_SIN_PERRO;
    else if (t->ocupante[destino] != OCUP_VACIA)
        r = MOV_DESTINO_OCUPADO;
    else {
        if (capturado != NULL)
            *capturado = t->ocupante[sobre];
        t->ocupante[sobre] = OCUP_VACIA;   /* el perro capturado se descubre al validar su origen */
        t->ocupante[destino] = OCUP_PUMA;
        t->ocupante[origen] = OCUP_VACIA;
        t->capturas++;
        r = MOV_OK;
    }
    liberar(t, cs, 3);
    return r;
}

int tablero_foto(Tablero *t, int foto[NUM_CASILLAS])
{
    int capturas;

    for (int i = 0; i < NUM_CASILLAS; i++)
        pthread_mutex_lock(&t->mtx[i]);
    memcpy(foto, t->ocupante, sizeof t->ocupante);
    capturas = t->capturas;
    for (int i = NUM_CASILLAS - 1; i >= 0; i--)
        pthread_mutex_unlock(&t->mtx[i]);
    return capturas;
}

/* ---- Reglas sobre una foto ---- */

int tablero_vecinos(int casilla, int vecinos[MAX_VECINOS])
{
    pthread_once(&tablas_listas, construir_tablas);
    if (!casilla_valida(casilla))
        return 0;
    memcpy(vecinos, vecino[casilla], (size_t)n_vecinos[casilla] * sizeof(int));
    return n_vecinos[casilla];
}

int tablero_saltos_desde(int casilla, Jugada out[MAX_VECINOS])
{
    pthread_once(&tablas_listas, construir_tablas);
    if (!casilla_valida(casilla))
        return 0;
    memcpy(out, salto[casilla], (size_t)n_saltos[casilla] * sizeof(Jugada));
    return n_saltos[casilla];
}

int tablero_posicion(const int foto[NUM_CASILLAS], int id)
{
    for (int i = 0; i < NUM_CASILLAS; i++)
        if (foto[i] == id)
            return i;
    return -1;
}

int tablero_jugadas_puma(const int foto[NUM_CASILLAS], Jugada out[MAX_JUGADAS])
{
    int pos = tablero_posicion(foto, OCUP_PUMA), n = 0;

    pthread_once(&tablas_listas, construir_tablas);
    if (pos < 0)
        return 0;
    for (int i = 0; i < n_vecinos[pos]; i++)
        if (foto[vecino[pos][i]] == OCUP_VACIA) {
            out[n].origen = pos;
            out[n].sobre = -1;
            out[n].destino = vecino[pos][i];
            n++;
        }
    for (int i = 0; i < n_saltos[pos]; i++)
        if (foto[salto[pos][i].sobre] >= 1 && foto[salto[pos][i].destino] == OCUP_VACIA)
            out[n++] = salto[pos][i];
    return n;
}

bool tablero_puma_acorralado(const int foto[NUM_CASILLAS])
{
    Jugada tmp[MAX_JUGADAS];

    if (tablero_posicion(foto, OCUP_PUMA) < 0)
        return false;
    return tablero_jugadas_puma(foto, tmp) == 0;
}

bool tablero_verificar(const Tablero *t, const int foto[NUM_CASILLAS], int capturas)
{
    int pumas = 0, perros = 0;
    unsigned visto = 0;

    for (int i = 0; i < NUM_CASILLAS; i++) {
        int o = foto[i];

        if (o == OCUP_VACIA)
            continue;
        if (o == OCUP_PUMA) {
            pumas++;
        } else if (o < 1 || o > NUM_PERROS || (visto & (1u << o))) {
            return false;
        } else {
            visto |= 1u << o;
            perros++;
        }
    }
    return pumas == 1 && perros == t->n_perros - capturas;
}

/* ---- Log ---- */

static char simbolo(int ocupante)
{
    if (ocupante == OCUP_VACIA) return '.';
    if (ocupante == OCUP_PUMA)  return 'P';
    return 'D';
}

int tablero_formatear(const int f[NUM_CASILLAS], char *buf, size_t n)
{
    #define S(i) simbolo(f[i])
    return snprintf(buf, n,
        "        %c\n"
        "      %c   %c\n"
        "    %c %c %c %c %c\n"
        "    %c %c %c %c %c\n"
        "    %c %c %c %c %c\n"
        "    %c %c %c %c %c\n"
        "    %c %c %c %c %c\n",
        S(0), S(1), S(2),
        S(3),  S(4),  S(5),  S(6),  S(7),
        S(8),  S(9),  S(10), S(11), S(12),
        S(13), S(14), S(15), S(16), S(17),
        S(18), S(19), S(20), S(21), S(22),
        S(23), S(24), S(25), S(26), S(27));
    #undef S
}

void tablero_log(int mesa_id, const char *evento, const int foto[NUM_CASILLAS])
{
    char buf[512];
    int n = snprintf(buf, sizeof buf, "[%6ld ms] [mesa %d] %.200s\n",
                     ms_desde_inicio(), mesa_id, evento);

    if (n < 0 || (size_t)n >= sizeof buf)
        return;
    n += tablero_formatear(foto, buf + n, sizeof buf - (size_t)n);
    if ((size_t)n >= sizeof buf)
        n = (int)sizeof buf - 1;
    log_bloque(buf, (size_t)n);
}