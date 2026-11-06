/* matchmaker.c: llegadas, emparejamiento y límite de mesas */
#define _POSIX_C_SOURCE 200809L
#include "matchmaker.h"
#include "mesa.h"
#include "util.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* El manejador solo marca la interrupción. */
static volatile sig_atomic_t torneo_interrumpido = 0;

static void manejar_sigint(int sig){
    (void)sig;
    torneo_interrumpido = 1;
}

/* Mesa en juego*/
typedef struct {
    pid_t pid;
    int   mesa_id;
    int   fd_lectura; /* lectura de su pipe */
} MesaActiva;

/* Estado del Matchmaker */
typedef struct {
    const Config  *cfg;
    unsigned int   rng;            /* retardos de llegada */
    MesaActiva    *activas;
    struct pollfd *fds;            /* para poll */
    int            max_activas;
    int            n_activas;
    int            proxima_mesa;   /* próxima mesa */
    int           *cola;           /* jugadores por orden de llegada */
    int            n_llegados;
    int            cabeza;         /* primero sin emparejar */
    int            cabeza_avisada; /* último par avisado en espera */
    int            terminadas;
    int            fallidas;
    bool           error;
} Matchmaker;

/* Retardo entre llegadas */
static int retardo_llegada(Matchmaker *mm){
    unsigned int rango =
        (unsigned int)(mm->cfg->retardo_max_ms - mm->cfg->retardo_min_ms) + 1u;

    return mm->cfg->retardo_min_ms + (int)((unsigned int)rand_r(&mm->rng) % rango);
}

/* Recoge la mesa i, lee su resultado y libera su cupo */
static void cerrar_mesa(Matchmaker *mm, int i){
    MesaActiva m = mm->activas[i];
    ResultadoMesa r = { ROL_NINGUNO, MOTIVO_NINGUNO, 0, 0 };
    int estado = 0;
    ssize_t n;
    pid_t w;


    /* Lectura del resultado, cerrar pipe y liberar hijo */
    n = read(m.fd_lectura, &r, sizeof r);

    while (n < 0 && errno == EINTR) {
        n = read(m.fd_lectura, &r, sizeof r);
    }

    close(m.fd_lectura);

    w = waitpid(m.pid, &estado, 0);

    while (w < 0 && errno == EINTR) {
        w = waitpid(m.pid, &estado, 0);
    }

    mm->activas[i] = mm->activas[mm->n_activas - 1];
    mm->n_activas--;

    if (w == m.pid && n == (ssize_t)sizeof r && WIFEXITED(estado) &&
        WEXITSTATUS(estado) == 0) {
        mm->terminadas++;
        log_evento("[matchmaker] mesa %d terminó: gana %s por %s (capturas %d, " "movimientos %d). Activas %d/%d", m.mesa_id, texto_rol(r.ganador), texto_motivo(r.motivo), r.capturas, r.movimientos, mm->n_activas, mm->max_activas);
    } else {
        mm->fallidas++;
        if (w == m.pid && WIFSIGNALED(estado))
            log_evento("[matchmaker] mesa %d FALLIDA: murió por la señal %d. " "Activas %d/%d", m.mesa_id, WTERMSIG(estado), mm->n_activas, mm->max_activas);
        else
            log_evento("[matchmaker] mesa %d FALLIDA: terminó sin resultado " "válido. Activas %d/%d", m.mesa_id, mm->n_activas, mm->max_activas);
    }
}

/* Espera una llegada o el fin de una mesa y recoge las terminadas. Devuelve -1 si poll falla */
static int atender_pipes(Matchmaker *mm, int timeout_ms){
    int n = mm->n_activas;

    if (n == 0 && timeout_ms < 0)
        return 0; 

    for (int i = 0; i < n; i++) {
        mm->fds[i].fd = mm->activas[i].fd_lectura;
        mm->fds[i].events = 0;
        mm->fds[i].revents = 0;
    }
    if (poll(mm->fds, (nfds_t)n, timeout_ms) < 0) {
        if (errno == EINTR)
            return 0;
        perror("[matchmaker] poll");
        return -1;
    }

    /* Revisa los eventos desde el último índice para poder retirar mesas activas sin alterar los índices pendientes */
    for (int i = n - 1; i >= 0; i--)
        if (mm->fds[i].revents & (POLLHUP | POLLERR | POLLNVAL))
            cerrar_mesa(mm, i);
    return 0;
}

/* Anota la llegada del jugador id */
static void registrar_llegada(Matchmaker *mm, int id){
    mm->cola[mm->n_llegados++] = id;
    if ((mm->n_llegados - mm->cabeza) % 2 == 1)
        log_evento("[matchmaker] llegó el jugador %d: esperando partida", id);
    else
        log_evento("[matchmaker] llegó el jugador %d: forma par con el jugador %d", id, mm->cola[mm->n_llegados - 2]);
}

/* Empareja a los dos primeros de la cola y lanza su mesa */
static int lanzar_mesa(Matchmaker *mm){
    int id_puma = mm->cola[mm->cabeza];       /* el primero siempre será el Puma */
    int id_perros = mm->cola[mm->cabeza + 1];
    int mesa_id = mm->proxima_mesa;
    int fd[2];
    pid_t pid;
    sigset_t bloqueadas, anteriores;

    if (pipe(fd) < 0) {
        perror("[matchmaker] pipe");
        return -1;
    }

    log_evento("[matchmaker] jugadores %d (Puma) y %d (Perros): cargando partida " "en la mesa %d", id_puma, id_perros, mesa_id);
    fflush(NULL); 

    /* Evita que Ctrl+C alcance al hijo antes de preparar sus señales. */
    sigemptyset(&bloqueadas);
    sigaddset(&bloqueadas, SIGINT);
    sigaddset(&bloqueadas, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &bloqueadas, &anteriores) == -1) {
        perror("[matchmaker] sigprocmask");
        close(fd[0]);
        close(fd[1]);
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        perror("[matchmaker] fork");
        sigprocmask(SIG_SETMASK, &anteriores, NULL);
        close(fd[0]);
        close(fd[1]);
        return -1;
    }
    if (pid == 0) {
        int rc;

        if (mesa_preparar_senales() != 0 ||
            sigprocmask(SIG_SETMASK, &anteriores, NULL) == -1)
            _exit(1);
        close(fd[0]);
        for (int i = 0; i < mm->n_activas; i++)
            close(mm->activas[i].fd_lectura);
        rc = mesa_ejecutar(mm->cfg, mesa_id, id_puma, id_perros, fd[1]);
        fflush(stdout);
        close(fd[1]);
        _exit(rc == 0 ? 0 : 1);
    }

    /* Registrar nueva mesa activa y actualizar cola de jugadores*/
    close(fd[1]); /*Cerrar escritura */
    mm->activas[mm->n_activas].pid = pid;
    mm->activas[mm->n_activas].mesa_id = mesa_id;
    mm->activas[mm->n_activas].fd_lectura = fd[0];
    mm->n_activas++;
    mm->cabeza += 2;
    mm->proxima_mesa++;
    log_evento("[matchmaker] mesa %d creada (pid %d). Activas %d/%d", mesa_id, (int)pid, mm->n_activas, mm->max_activas);
    sigprocmask(SIG_SETMASK, &anteriores, NULL);
    return 0;
}


/*Iniciar matchmaking y reservar memoria para las mesas*/
int matchmaker_ejecutar(const Config *cfg, int n_jugadores, int max_mesas){
    Matchmaker mm;
    int cap = (max_mesas < n_jugadores / 2) ? max_mesas : n_jugadores / 2;
    int llegadas = 0;
    long t_llegada;
    struct sigaction sa = {0}, sa_anterior;

    torneo_interrumpido = 0;
    sa.sa_handler = manejar_sigint;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, &sa_anterior) == -1) {
        perror("[matchmaker] sigaction");
        return 1;
    }

    if (cap < 1)
        cap = 1;
    memset(&mm, 0, sizeof mm);
    mm.cfg = cfg;
    mm.rng = cfg->semilla;
    mm.max_activas = max_mesas;
    mm.proxima_mesa = 1;
    mm.cabeza_avisada = -1;
    mm.activas = calloc((size_t)cap, sizeof *mm.activas);
    mm.fds = calloc((size_t)cap, sizeof *mm.fds);
    mm.cola = calloc((size_t)n_jugadores, sizeof *mm.cola);

    /*Si alguna reserva falla, se tira error y se libera la memoria */
    if (mm.activas == NULL || mm.fds == NULL || mm.cola == NULL) {
        perror("[matchmaker] calloc");
        free(mm.activas);
        free(mm.fds);
        free(mm.cola);
        sigaction(SIGINT, &sa_anterior, NULL);
        return 1;
    }

    log_evento("[matchmaker] inicio: %d jugadores, máximo %d mesas simultáneas",
               n_jugadores, max_mesas);
    t_llegada = ms_desde_inicio() + retardo_llegada(&mm);

    /*Ciclo principal del matchmaking, se espera la llegada o el termino de una mesa*/
    while (!mm.error && !torneo_interrumpido &&
           (llegadas < n_jugadores || mm.n_llegados - mm.cabeza >= 2 || mm.n_activas > 0)) {
        int espera = -1; /* sin llegadas: solo mesas */

        /*Cálculo llegada del próximo jugador*/
        if (llegadas < n_jugadores) {
            long falta = t_llegada - ms_desde_inicio();
            espera = (falta > 0) ? (int)falta : 0;
        }
        if (atender_pipes(&mm, espera) < 0) {
            mm.error = true;
            break;
        }

        /* Registrar jugadores en espera*/
        while (!torneo_interrumpido && llegadas < n_jugadores &&
               ms_desde_inicio() >= t_llegada) {
            llegadas++;
            registrar_llegada(&mm, llegadas);
            t_llegada += retardo_llegada(&mm);
        }

        /* Creación de mesas*/
        while (!torneo_interrumpido && mm.n_llegados - mm.cabeza >= 2 &&
               mm.n_activas < mm.max_activas && !mm.error){
            if (lanzar_mesa(&mm) < 0)
                mm.error = true;
        }
        if (mm.n_llegados - mm.cabeza >= 2 && mm.n_activas >= mm.max_activas && mm.cabeza_avisada != mm.cabeza) {
            log_evento("[matchmaker] las %d mesas están ocupadas: el par (%d y %d) " "espera mesa", mm.max_activas, mm.cola[mm.cabeza], mm.cola[mm.cabeza + 1]);
            mm.cabeza_avisada = mm.cabeza;
        }
    }

    if (torneo_interrumpido) {
        log_evento("[matchmaker] Ctrl+C recibido: se interrumpen las mesas activas");
        for (int i = 0; i < mm.n_activas; i++)
            if (kill(mm.activas[i].pid, SIGTERM) == -1 && errno != ESRCH) {
                perror("[matchmaker] SIGTERM");
                mm.error = true;
            }
    } else if (mm.error)
        log_evento("[matchmaker] error: no se crean más mesas");
    else if (mm.cabeza < mm.n_llegados)
        log_evento("[matchmaker] el jugador %d queda sin pareja y se descarta", mm.cola[mm.cabeza]);

    /* Espera las mesas que siguen activas */
    while (mm.n_activas > 0)
        if (atender_pipes(&mm, -1) < 0)
            break;
    for (int i = 0; i < mm.n_activas; i++)
        close(mm.activas[i].fd_lectura);

    log_evento("[matchmaker] fin: %d mesas terminadas, %d fallidas", mm.terminadas, mm.fallidas);

    free(mm.activas);
    free(mm.fds);
    free(mm.cola);
    int rc = torneo_interrumpido ? 130 : mm.error ? 1 : 0;
    sigaction(SIGINT, &sa_anterior, NULL);
    return rc;
}