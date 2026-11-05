/* mesa.c: una mesa = un proceso (lo crea el Matchmaker) con 13 hilos de fichas y un Juez.
Cada mesa es un proceso con un juez, que es el hilo principal y 13 hilos de fichas
Aquí se coordinan los movimientos de las fichas, las victorias y el cierre de la partida
*/
#define _POSIX_C_SOURCE 200809L
#include "mesa.h"
#include "tablero.h"
#include "util.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Máximo de 60 segundos por partida, a menos que se cambie al compilar. */
#ifndef TIEMPO_LIMITE_MS
#define TIEMPO_LIMITE_MS 60000
#endif
/* 1 = una línea de log por cada movimiento; 0 = solo capturas, tableros y eventos clave */
#ifndef LOG_MOVS
#define LOG_MOVS 1
#endif
#define TABLERO_CADA_MOVS 15   /* se imprime el tablero completo cada tantos movimientos */
#define REVISION_MS       100  /* el Juez revisa la bandera de Ctrl+C al menos cada tanto */

/* Bandera para avisar al Juez de una interrupción. */
static volatile sig_atomic_t mesa_interrumpida = 0;

/* Convierte el rol en un nombre para mostrarlo en los mensajes. */
const char *texto_rol(Rol r)
{
    switch (r) {
    case ROL_PUMA:   return "Puma";
    case ROL_PERROS: return "Perros";
    default:         return "nadie";
    }
}

/* Devuelve el texto que explica por qué terminó una partida. */
const char *texto_motivo(Motivo m)
{
    switch (m) {
    case MOTIVO_CAPTURAS:       return "capturas";
    case MOTIVO_ACORRALAMIENTO: return "acorralamiento";
    case MOTIVO_ABANDONO:       return "abandono";
    case MOTIVO_INTERRUPCION:   return "interrupción";
    case MOTIVO_TIEMPO:         return "tiempo";
    default:                    return "ninguno";
    }
}


/* Datos que comparten el Juez y los hilos de las fichas. */
typedef struct {
    int           id;              /* Número de esta mesa. */
    const Config *cfg;             /* Configuración que se usa en la partida. */
    Tablero       tablero;         /* Posiciones de todas las fichas. */

    pthread_mutex_t estado;        /* protege todo lo que sigue */
    pthread_cond_t  cond_juez;     /* las fichas despiertan al Juez */
    pthread_cond_t  cond_fichas;   /* el Juez despierta a las fichas cuando termina la partida */
    bool            evento;        /* hubo un movimiento que el Juez debe revisar */
    bool            fin;           /* la partida terminó */
    int             movs[3];       /* movimientos exitosos por rol (índice = Rol) */
    Rol             caido;         /* [FALLOS] jugador que falló, o ROL_NINGUNO */
    FallaSimulada   falla;         /* [FALLOS] falla programada para ESTA mesa (mesa = 0: ninguna) */
    ResultadoMesa   res;           /* Ganador, motivo y estadísticas finales. */
} Mesa;


/* Datos que necesita cada hilo para mover su propia ficha. */
typedef struct {
    Mesa    *m;      /* Mesa a la que pertenece. */
    Rol      rol;    /* Puma o Perros. */
    int      id;     /* OCUP_PUMA, o 1..12 para los perros */
    int      pos;    /* casilla actual (solo la toca su propio hilo) */
    unsigned rng;    /* Semilla propia para elegir movimientos y retardos. */
} Ficha;


/* Calcula una hora futura sumando ms al reloj actual. */
static struct timespec en_ms(int ms){
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) {
        t.tv_sec++;
        t.tv_nsec -= 1000000000L;
    }
    return t;
}

/* Comprueba si ya se alcanzó el tiempo límite indicado. */
static bool vencido(const struct timespec *t){
    struct timespec a;

    clock_gettime(CLOCK_MONOTONIC, &a);
    return a.tv_sec > t->tv_sec || (a.tv_sec == t->tv_sec && a.tv_nsec >= t->tv_nsec);
}


/* Devuelve el equipo contrario al recibido. */
static Rol opuesto(Rol r){
    return r == ROL_PUMA ? ROL_PERROS : ROL_PUMA;
}

/* Indica si la ficha debe dejar de jugar por fin de partida o falla
Se llama mientras está tomado el mutex 'estado'. */
static bool debo_salir(const Mesa *m, Rol rol){
    return m->fin || m->caido == rol;
}

/* Elige cuánto esperará esta ficha antes de intentar moverse. */
static int retardo(Ficha *f){
    const Config *c = f->m->cfg;
    unsigned rango = (unsigned)(c->retardo_max_ms - c->retardo_min_ms) + 1u;

    return c->retardo_min_ms + (int)(rand_r(&f->rng) % rango);
}

/* Espera 'ms' o hasta que la partida termine. false = la ficha debe salir.
 * Es una espera pasiva (cond_timedwait): no hay busy waiting y se corta apenas hay fin */
static bool esperar(Mesa *m, Rol rol, int ms){
    struct timespec limite = en_ms(ms);
    bool sigo;

    pthread_mutex_lock(&m->estado);
    while (!debo_salir(m, rol))
        if (pthread_cond_timedwait(&m->cond_fichas, &m->estado, &limite) == ETIMEDOUT)
            break;
    sigo = !debo_salir(m, rol);
    pthread_mutex_unlock(&m->estado);
    return sigo;
}

/* Registra un movimiento que sí se realizó, avisa al Juez y revisa si corresponde activar una falla simulada. También decide cuándo imprimir
el tablero. Devuelve false si esta ficha debe dejar de jugar. */
static bool registrar_movimiento(Mesa *m, Rol rol){
    bool sigo;
    int total;

    pthread_mutex_lock(&m->estado);
    int n = ++m->movs[rol];/* Movimientos acumulados por este jugador. */
    total = m->movs[ROL_PUMA] + m->movs[ROL_PERROS];/* Total para decidir cuándo imprimir. */
    
     /* Si este jugador llegó al movimiento indicado, se activa su falla. */
    if (!m->fin && m->caido == ROL_NINGUNO && m->falla.mesa == m->id && m->falla.rol == rol && n == m->falla.movimiento) {
        m->caido = rol;
        pthread_cond_broadcast(&m->cond_fichas);   /* despierta a los hilos de ese jugador */
    }
    m->evento = true;
    pthread_cond_signal(&m->cond_juez);
    sigo = !debo_salir(m, rol);
    pthread_mutex_unlock(&m->estado);

    if (total % TABLERO_CADA_MOVS == 0) { /* La foto se toma fuera del mutex 'estado'. */  
        int foto[NUM_CASILLAS];

        tablero_foto(&m->tablero, foto);
        tablero_log(m->id, "estado del tablero", foto);
    }
    return sigo;
}


/* Movimiento de las fichas: cada una funciona en su propio hilo. */

/* Cada Perro espera, mira sus casillas vecinas y trata de moverse a una libre.
Si lo capturan o termina la partida, su hilo se detiene. */
static void *hilo_perro(void *arg){
    Ficha *f = arg;
    Mesa *m = f->m;

    while (esperar(m, ROL_PERROS, retardo(f))) {
        /* foto: tablero visto; v: vecinos; libres: destinos disponibles; nl: cuántos hay. */
        int foto[NUM_CASILLAS], v[MAX_VECINOS], libres[MAX_VECINOS], nl = 0;
        int n;
        ResMov r;


        tablero_foto(&m->tablero, foto);
        if (foto[f->pos] != f->id)
            break;  /* Este Perro ya fue capturado. */
        n = tablero_vecinos(f->pos, v);
        for (int i = 0; i < n; i++)
            if (foto[v[i]] == OCUP_VACIA)
                libres[nl++] = v[i];
        if (nl == 0)
            continue;

        /* Otra ficha pudo moverse desde la foto: tablero_mover lo vuelve a comprobar. */
        int destino = libres[rand_r(&f->rng) % (unsigned)nl];

        r = tablero_mover(&m->tablero, f->id, f->pos, destino);
        if (r == MOV_CERRADO)
            break;                                 /* El tablero ya está cerrado. */
        if (r == MOV_ORIGEN_PERDIDO)
            break;                                 /* Lo capturaron antes de moverse. */
        if (r != MOV_OK)
            continue;                              /* El destino dejó de estar libre. */
#if LOG_MOVS
        log_evento("[mesa %d] perro %d: casilla %d -> %d", m->id, f->id, f->pos, destino);
#endif
        f->pos = destino;
        if (!registrar_movimiento(m, ROL_PERROS))
            break;
    }
    return NULL;
}


/* El Puma busca movimientos posibles. Si puede capturar, prioriza el salto, si no, elige un movimiento normal. */
static void *hilo_puma(void *arg){
    Ficha *f = arg;
    Mesa *m = f->m;

    while (esperar(m, ROL_PUMA, retardo(f))) {
        int foto[NUM_CASILLAS], saltos[MAX_JUGADAS], ns = 0;
        /* saltos guarda cuáles jugadas permiten capturar; ns cuenta cuántas. */

        Jugada js[MAX_JUGADAS], j; /* js: opciones posibles; j: jugada elegida. */
        int n, capturado = -1;/* n: opciones disponibles, capturado: perro eliminado. */
        ResMov r;

        tablero_foto(&m->tablero, foto);
        n = tablero_jugadas_puma(foto, js);
        if (n == 0)
            continue;                              /* sin movimientos por lo que el juez declara el acorralamiento */
        for (int i = 0; i < n; i++)
            if (js[i].sobre >= 0)
                saltos[ns++] = i;
        j = ns > 0 ? js[saltos[rand_r(&f->rng) % (unsigned)ns]]
                   : js[rand_r(&f->rng) % (unsigned)n];

        if (j.sobre >= 0)
            r = tablero_saltar(&m->tablero, j.origen, j.sobre, j.destino, &capturado);
        else
            r = tablero_mover(&m->tablero, OCUP_PUMA, j.origen, j.destino);
        if (r == MOV_CERRADO)
            break;                                 /* la partida ya terminó */
        if (r != MOV_OK)
            continue;                              /* El tablero cambió por lo que se intenta de nuevo. */

        if (j.sobre >= 0)
            log_evento("[mesa %d] el Puma captura al perro %d (casilla %d -> %d)",
                       m->id, capturado, j.origen, j.destino);
#if LOG_MOVS
        else
            log_evento("[mesa %d] Puma: casilla %d -> %d", m->id, j.origen, j.destino);
#endif
        f->pos = j.destino;
        if (!registrar_movimiento(m, ROL_PUMA))
            break;
    }
    return NULL;
}

/* El Juez observa el tablero y decide cuándo termina la partida. */

/* Cierra el tablero para que ninguna ficha pueda seguir moviéndose. Se llama sin tener bloqueado 'estado'. */
static void congelar(Mesa *m){
    int foto[NUM_CASILLAS];

    tablero_cerrar(&m->tablero, foto);
}

/* Guarda quién ganó y por qué, marca el fin y avisa a todos los hilos. */
static void declarar(Mesa *m, Rol ganador, Motivo motivo){
    pthread_mutex_lock(&m->estado);
    m->fin = true;
    m->res.ganador = ganador;
    m->res.motivo = motivo;
    pthread_cond_broadcast(&m->cond_fichas);
    pthread_mutex_unlock(&m->estado);
}

/* El hilo principal actúa como juez. espera avisos de las fichas y revisa
si ganó alguien, hubo una falla, se acabó el tiempo o llegó una interrupción. */
static void juez(Mesa *m){
    struct timespec limite = en_ms(TIEMPO_LIMITE_MS);
    int foto[NUM_CASILLAS], capturas;

    for (;;) {
        Rol caido;
        Victoria victoria;

        pthread_mutex_lock(&m->estado);
        while (!m->evento && m->caido == ROL_NINGUNO && !mesa_interrumpida && !vencido(&limite)) {
            struct timespec corte = en_ms(REVISION_MS);

            pthread_cond_timedwait(&m->cond_juez, &m->estado, &corte);
        }
        m->evento = false;
        caido = m->caido;
        pthread_mutex_unlock(&m->estado);   /* se suelta 'estado' antes de tocar las casillas */

        if (mesa_interrumpida) {
            congelar(m);
            declarar(m, ROL_NINGUNO, MOTIVO_INTERRUPCION);
            return;
        }
        /* Si un jugador falla, gana el equipo contrario por abandono. */
        if (caido != ROL_NINGUNO) {
            log_evento("[mesa %d] el jugador %s falla tras %d movimientos: gana %s por abandono",
                       m->id, texto_rol(caido), m->falla.movimiento, texto_rol(opuesto(caido)));
            congelar(m);
            declarar(m, opuesto(caido), MOTIVO_ABANDONO);
            return;
        }

        /* Foto, veredicto y cierre del tablero en un solo paso*/
        victoria = tablero_evaluar_y_cerrar(&m->tablero, foto, &capturas);
        if (!tablero_verificar(&m->tablero, foto, capturas))
            log_evento("[mesa %d] ERROR: el tablero violó una invariante", m->id);
        if (victoria == VIC_PUMA) {
            declarar(m, ROL_PUMA, MOTIVO_CAPTURAS);
            return;
        }
        if (victoria == VIC_PERROS) {
            declarar(m, ROL_PERROS, MOTIVO_ACORRALAMIENTO);
            return;
        }
         /* Si se acabó el tiempo, la partida termina sin ganador. */
        if (vencido(&limite)) {
            congelar(m);
            declarar(m, ROL_NINGUNO, MOTIVO_TIEMPO);
            return;
        }
    }
}

/* Inicio y cierre de una mesa completa. */

/* Prepara el tablero, crea los 13 hilos y hace que el hilo principal sea el juez.
 Al terminar, espera a todas las fichas y envía el resultado al matchmaker por el pipe (fd_resultado). */

int mesa_ejecutar(const Config *cfg, int mesa_id, int id_puma, int id_perros, int fd_resultado){
    Mesa m;                        /* Información compartida de esta partida. */
    Ficha fichas[1 + NUM_PERROS];  /* Datos de las 13 fichas. */
    pthread_t th[1 + NUM_PERROS];  /* Identificadores de los hilos creados. */
    pthread_condattr_t ca;         /* Ajustes del reloj de las esperas. */
    int foto[NUM_CASILLAS], n_fichas, creados = 0, capturas;
    /* foto: tablero copiado, n_fichas: total, creados: hilos que alcanzaron a iniciarse. */
    bool error = false;            /* Indica si falló la creación de algún hilo. */
    ssize_t w;                     /* Bytes enviados al Matchmaker. */

    memset(&m, 0, sizeof m);
    m.id = mesa_id;
    m.cfg = cfg;
    m.caido = ROL_NINGUNO;
    m.evento = true;                       /* el juez revisa el estado inicial */
    if (cfg->falla.mesa == mesa_id)
        m.falla = cfg->falla;              /* Solo se aplica la falla de esta mesa.*/

    /* El juez inicializa el tablero y las primitivas de sincronización */
    if (tablero_iniciar(&m.tablero, cfg->disposicion) != 0) {
        log_evento("[mesa %d] ERROR: disposición inicial inválida", mesa_id);
        return -1;
    }
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_mutex_init(&m.estado, NULL);
    pthread_cond_init(&m.cond_juez, &ca);
    pthread_cond_init(&m.cond_fichas, &ca);
    pthread_condattr_destroy(&ca);

    log_evento("[mesa %d] comienza: Puma = jugador %d, Perros = jugador %d (límite %d ms)",
               mesa_id, id_puma, id_perros, TIEMPO_LIMITE_MS);
    tablero_foto(&m.tablero, foto);
    tablero_log(mesa_id, "tablero inicial", foto);

    /* Un hilo para el puma y uno por cada perro */
    n_fichas = 1 + m.tablero.n_perros;
    for (int i = 0; i < n_fichas; i++) {
        fichas[i].m = &m;
        fichas[i].rol = (i == 0) ? ROL_PUMA : ROL_PERROS;
        fichas[i].id = (i == 0) ? OCUP_PUMA : i;
        fichas[i].pos = tablero_posicion(foto, fichas[i].id);
        fichas[i].rng = cfg->semilla ^ (0x9E3779B9u * (unsigned)mesa_id)
                                     ^ (2654435761u * (unsigned)(i + 1));
        if (pthread_create(&th[i], NULL, i == 0 ? hilo_puma : hilo_perro, &fichas[i]) != 0) {
            log_evento("[mesa %d] ERROR: no se pudo crear el hilo %d", mesa_id, i);
            error = true;
            break;
        }
        creados++;
    }

    /* El juez arbitra hasta que haya un resultado */
    if (error) {
        congelar(&m);
        declarar(&m, ROL_NINGUNO, MOTIVO_NINGUNO);
    }
    else
        juez(&m);

    /* Espera a que terminen las fichas y luego libera lo que usó la mesa. */
    for (int i = 0; i < creados; i++)
        pthread_join(th[i], NULL);
    capturas = tablero_foto(&m.tablero, foto);
    m.res.capturas = capturas;
    m.res.movimientos = atomic_load(&m.tablero.movimientos);
    if (m.res.movimientos != m.movs[ROL_PUMA] + m.movs[ROL_PERROS])
        log_evento("[mesa %d] ERROR contador tablero=%d y mesa=%d", m.id,
                   m.res.movimientos, m.movs[ROL_PUMA] + m.movs[ROL_PERROS]);
    tablero_log(mesa_id, "tablero final", foto);
    log_evento("[mesa %d] termina: gana %s por %s", mesa_id, texto_rol(m.res.ganador),
               texto_motivo(m.res.motivo));

    tablero_destruir(&m.tablero);
    pthread_cond_destroy(&m.cond_fichas);
    pthread_cond_destroy(&m.cond_juez);
    pthread_mutex_destroy(&m.estado);
    if (error)
        return -1;

    /* Envía el resultado de la partida al Matchmaker por el pipe. */
    w = write(fd_resultado, &m.res, sizeof m.res);
    while (w < 0 && errno == EINTR)
        w = write(fd_resultado, &m.res, sizeof m.res);
    return (w == (ssize_t)sizeof m.res) ? 0 : -1;
}
