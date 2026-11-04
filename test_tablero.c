/* Pruebas del tablero.
 * gcc -Wall -Wextra -std=c17 -pthread test_tablero.c tablero.c util.c -o test_tablero && ./test_tablero */
#define _POSIX_C_SOURCE 200809L
#include "tablero.h"
#include "util.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fallos = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("  FALLO: %s (línea %d)\n", msg, __LINE__); fallos++; } } while (0)

static void dormir_us(unsigned us)
{
    struct timespec ts = { 0, (long)us * 1000L };
    nanosleep(&ts, NULL);
}

static void disposicion_estandar(Celda d[NUM_CASILLAS])
{
    for (int i = 0; i < NUM_CASILLAS; i++)
        d[i] = CELDA_VACIA;
    d[0] = CELDA_PUMA;
    for (int i = NUM_CASILLAS - NUM_PERROS; i < NUM_CASILLAS; i++)
        d[i] = CELDA_PERRO;
}

static bool existe_salto(int a, int sobre, int dest)
{
    Jugada js[MAX_VECINOS];
    int n = tablero_saltos_desde(a, js);

    for (int i = 0; i < n; i++)
        if (js[i].sobre == sobre && js[i].destino == dest)
            return true;
    return false;
}

/* ---------- 1. Geometría ---------- */
static void prueba_geometria(void)
{
    int v[MAX_VECINOS], w[MAX_VECINOS], aristas = 0, saltos = 0;
    Jugada js[MAX_VECINOS];

    printf("1. Geometría\n");
    for (int c = 0; c < NUM_CASILLAS; c++) {
        int n = tablero_vecinos(c, v);

        aristas += n;
        for (int i = 0; i < n; i++) {
            int m = tablero_vecinos(v[i], w), simetrica = 0;
            for (int j = 0; j < m; j++)
                if (w[j] == c)
                    simetrica = 1;
            CHECK(simetrica, "adyacencia no simétrica");
        }
        saltos += tablero_saltos_desde(c, js);
    }
    CHECK(aristas == 96, "debe haber 48 conexiones (96 contando ambos sentidos)");
    CHECK(saltos == 70, "debe haber 70 saltos posibles");

    int n0 = tablero_vecinos(0, v);
    CHECK(n0 == 3 && v[0] + v[1] + v[2] == 1 + 2 + 5, "el trono conecta con 1, 2 y 5");
    CHECK(existe_salto(0, 1, 3) && existe_salto(3, 1, 0), "diagonal izquierda 0-1-3");
    CHECK(existe_salto(0, 2, 7) && existe_salto(7, 2, 0), "diagonal derecha 0-2-7");
    CHECK(existe_salto(0, 5, 10) && existe_salto(10, 5, 0), "vertical 0-5-10");
    CHECK(existe_salto(1, 4, 9) && existe_salto(9, 4, 1), "vertical 1-4-9");
    CHECK(existe_salto(2, 6, 11) && existe_salto(11, 6, 2), "vertical 2-6-11");
    CHECK(existe_salto(3, 4, 5) && existe_salto(13, 18, 23), "saltos de la grilla");
    CHECK(!existe_salto(0, 1, 2), "no se salta de 0 a 2 sobre 1 (no están en línea)");
    CHECK(!existe_salto(4, 1, 0), "no se salta de 4 a 0 sobre 1 (no están en línea)");
    CHECK(existe_salto(3, 8, 13), "vertical en la grilla 3-8-13");
}

/* ---------- 2. Reglas y dibujo ---------- */
static void prueba_reglas(void)
{
    Celda d[NUM_CASILLAS];
    Tablero t;
    int foto[NUM_CASILLAS], capturado = -9;
    char buf[512];
    const char *esperado =
        "        P\n"
        "      .   .\n"
        "    . . . . .\n"
        "    . . . . .\n"
        "    . . . D D\n"
        "    D D D D D\n"
        "    D D D D D\n";

    printf("2. Reglas y dibujo\n");
    disposicion_estandar(d);
    CHECK(tablero_iniciar(&t, d) == 0, "iniciar");
    tablero_foto(&t, foto);
    tablero_formatear(foto, buf, sizeof buf);
    CHECK(strcmp(buf, esperado) == 0, "el dibujo inicial no coincide");
    CHECK(tablero_verificar(&t, foto, 0), "invariantes al inicio");

    CHECK(tablero_mover(&t, OCUP_PUMA, 0, 5) == MOV_OK, "puma 0 -> 5");
    CHECK(tablero_mover(&t, OCUP_PUMA, 5, 0) == MOV_OK, "puma 5 -> 0");
    CHECK(tablero_mover(&t, OCUP_PUMA, 0, 3) == MOV_ILEGAL, "0 -> 3 no es adyacente");
    CHECK(tablero_mover(&t, 1, 16, 21) == MOV_DESTINO_OCUPADO, "perro hacia casilla ocupada");
    CHECK(tablero_mover(&t, 1, 16, 11) == MOV_OK, "perro 1 avanza 16 -> 11");
    CHECK(tablero_mover(&t, 1, 16, 11) == MOV_ORIGEN_PERDIDO, "perro 1 ya no está en 16");

    /* Captura: puma en 10, perro en 11, destino 12 libre */
    CHECK(tablero_mover(&t, OCUP_PUMA, 0, 5) == MOV_OK, "puma a 5");
    CHECK(tablero_mover(&t, OCUP_PUMA, 5, 10) == MOV_OK, "puma a 10");
    CHECK(tablero_saltar(&t, 10, 11, 12, &capturado) == MOV_OK && capturado == 1, "captura al perro 1");
    CHECK(tablero_mover(&t, 1, 11, 6) == MOV_ORIGEN_PERDIDO, "el perro capturado se descubre solo");
    CHECK(tablero_saltar(&t, 12, 17, 22, NULL) == MOV_DESTINO_OCUPADO, "salto con destino ocupado");
    CHECK(tablero_saltar(&t, 12, 7, 2, NULL) == MOV_ILEGAL, "salto fuera de línea");
    int c = tablero_foto(&t, foto);
    CHECK(c == 1 && tablero_verificar(&t, foto, c), "invariantes tras capturar");
    tablero_log(1, "puma captura al perro 1 (10 -> 12)", foto);
    tablero_destruir(&t);

    /* Puma acorralado en el trono */
    for (int i = 0; i < NUM_CASILLAS; i++)
        d[i] = CELDA_VACIA;
    d[0] = CELDA_PUMA;
    d[1] = d[2] = d[5] = CELDA_PERRO;        /* vecinos del trono */
    d[3] = d[7] = d[10] = CELDA_PERRO;       /* detrás de cada uno: saltos bloqueados */
    tablero_iniciar(&t, d);
    tablero_foto(&t, foto);
    CHECK(tablero_puma_acorralado(foto), "puma acorralado");
    tablero_destruir(&t);

    d[10] = CELDA_VACIA;                     /* ahora puede saltar sobre 5 hacia 10 */
    tablero_iniciar(&t, d);
    tablero_foto(&t, foto);
    CHECK(!tablero_puma_acorralado(foto), "puma con un salto libre no está acorralado");
    tablero_destruir(&t);
}

/* ---------- 3. Carrera: 4 perros pelean por la misma casilla ---------- */
typedef struct {
    Tablero *t;
    pthread_barrier_t *b;
    int id, origen, destino;
    ResMov res;
} Intento;

static void *hilo_intento(void *arg)
{
    Intento *a = arg;

    pthread_barrier_wait(a->b);
    a->res = tablero_mover(a->t, a->id, a->origen, a->destino);
    return NULL;
}

static void prueba_carrera(void)
{
    const int orig[4] = { 5, 9, 11, 15 };
    int rondas_mal = 0;

    printf("3. Carrera por una casilla (2000 rondas, 4 hilos)\n");
    for (int ronda = 0; ronda < 2000; ronda++) {
        Celda d[NUM_CASILLAS];
        Tablero t;
        pthread_barrier_t b;
        pthread_t th[4];
        Intento in[4];
        int ok = 0, ocupado = 0, foto[NUM_CASILLAS];

        for (int i = 0; i < NUM_CASILLAS; i++)
            d[i] = CELDA_VACIA;
        d[27] = CELDA_PUMA;
        for (int k = 0; k < 4; k++)
            d[orig[k]] = CELDA_PERRO;        /* ids 1..4 en orden de casilla */
        tablero_iniciar(&t, d);
        pthread_barrier_init(&b, NULL, 4);
        for (int k = 0; k < 4; k++) {
            in[k] = (Intento){ &t, &b, k + 1, orig[k], 10, MOV_ILEGAL };
            pthread_create(&th[k], NULL, hilo_intento, &in[k]);
        }
        for (int k = 0; k < 4; k++) {
            pthread_join(th[k], NULL);
            ok += in[k].res == MOV_OK;
            ocupado += in[k].res == MOV_DESTINO_OCUPADO;
        }
        int c = tablero_foto(&t, foto);
        if (ok != 1 || ocupado != 3 || foto[10] < 1 || !tablero_verificar(&t, foto, c))
            rondas_mal++;
        pthread_barrier_destroy(&b);
        tablero_destruir(&t);
    }
    CHECK(rondas_mal == 0, "en alguna ronda no ganó exactamente un perro");
}

/* ---------- 4. Estrés: Puma + 12 perros + juez ---------- */
typedef struct {
    Tablero *t;
    int id, pos;
    unsigned rng;
    atomic_bool *fin;
    int capturas;
    long intentos;
} Ficha;

static void *hilo_perro(void *arg)
{
    Ficha *f = arg;
    int v[MAX_VECINOS];

    while (!atomic_load(f->fin)) {
        int n = tablero_vecinos(f->pos, v);
        int d = v[rand_r(&f->rng) % (unsigned)n];
        ResMov r = tablero_mover(f->t, f->id, f->pos, d);

        f->intentos++;
        if (r == MOV_OK)
            f->pos = d;
        else if (r == MOV_ORIGEN_PERDIDO)
            break;                           /* me capturaron: el hilo termina solo */
        dormir_us(rand_r(&f->rng) % 300);
    }
    return NULL;
}

static void *hilo_puma(void *arg)
{
    Ficha *f = arg;
    Jugada js[MAX_JUGADAS];
    int foto[NUM_CASILLAS];

    while (!atomic_load(f->fin)) {
        tablero_foto(f->t, foto);
        int n = tablero_jugadas_puma(foto, js);

        if (n == 0) {                        /* acorralado: ganan los perros */
            atomic_store(f->fin, true);
            break;
        }
        Jugada j = js[rand_r(&f->rng) % (unsigned)n];
        ResMov r = (j.sobre < 0) ? tablero_mover(f->t, OCUP_PUMA, j.origen, j.destino)
                                 : tablero_saltar(f->t, j.origen, j.sobre, j.destino, NULL);

        f->intentos++;
        if (r == MOV_OK && j.sobre >= 0 && ++f->capturas >= CAPTURAS_VICTORIA)
            atomic_store(f->fin, true);
        dormir_us(rand_r(&f->rng) % 300);
    }
    return NULL;
}

static void prueba_estres(void)
{
    Celda d[NUM_CASILLAS];
    Tablero t;
    atomic_bool fin = false;
    pthread_t th[13];
    Ficha f[13];
    int foto[NUM_CASILLAS], mal = 0;
    long fotos = 0, total = 0;
    int partidas = 0, pumas = 0, perros = 0;

    printf("4. Estrés (50 partidas con 13 hilos + juez)\n");
    for (int p = 0; p < 50; p++) {
        disposicion_estandar(d);
        tablero_iniciar(&t, d);
        atomic_store(&fin, false);

        tablero_foto(&t, foto);              /* posiciones iniciales */
        f[0] = (Ficha){ &t, OCUP_PUMA, tablero_posicion(foto, OCUP_PUMA), 1000u + (unsigned)p, &fin, 0, 0 };
        pthread_create(&th[0], NULL, hilo_puma, &f[0]);
        for (int k = 1; k <= NUM_PERROS; k++) {
            f[k] = (Ficha){ &t, k, tablero_posicion(foto, k), 77u * (unsigned)k + (unsigned)p, &fin, 0, 0 };
            pthread_create(&th[k], NULL, hilo_perro, &f[k]);
        }

        /* Juez: foto consistente + invariantes + condiciones de victoria */
        long t0 = ms_desde_inicio();
        while (!atomic_load(&fin) && ms_desde_inicio() - t0 < 3000) {
            int c = tablero_foto(&t, foto);

            fotos++;
            if (!tablero_verificar(&t, foto, c))
                mal++;
            if (c >= CAPTURAS_VICTORIA || tablero_puma_acorralado(foto))
                atomic_store(&fin, true);
            dormir_us(500);
        }
        atomic_store(&fin, true);
        for (int k = 0; k <= NUM_PERROS; k++) {
            pthread_join(th[k], NULL);
            total += f[k].intentos;
        }
        int c = tablero_foto(&t, foto);
        if (!tablero_verificar(&t, foto, c) || c != f[0].capturas)
            mal++;
        pumas += c >= CAPTURAS_VICTORIA;
        perros += tablero_puma_acorralado(foto);
        partidas++;
        tablero_destruir(&t);
    }
    printf("   %d partidas, %ld movimientos intentados, %ld fotos del juez\n", partidas, total, fotos);
    printf("   terminaron por capturas: %d, por acorralamiento: %d, por tiempo: %d\n",
           pumas, perros, partidas - pumas - perros);
    CHECK(mal == 0, "se violó algún invariante del tablero");
}

int main(void)
{
    prueba_geometria();
    prueba_reglas();
    prueba_carrera();
    prueba_estres();
    printf(fallos == 0 ? "\nTODO OK\n" : "\n%d FALLO(S)\n", fallos);
    return fallos != 0;
}