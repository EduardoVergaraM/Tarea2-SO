/* util.c: espera en ms y log con tiempo */
#define _POSIX_C_SOURCE 200809L
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static struct timespec inicio;
static pthread_once_t reloj_listo = PTHREAD_ONCE_INIT;
static int fd_log = -1;

static void iniciar_reloj(void)
{
    clock_gettime(CLOCK_MONOTONIC, &inicio);
}

/* Ms desde el primer uso; los hijos de fork lo heredan */
long ms_desde_inicio(void)
{
    struct timespec ahora;

    pthread_once(&reloj_listo, iniciar_reloj);
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    return (long)(ahora.tv_sec - inicio.tv_sec) * 1000L + (ahora.tv_nsec - inicio.tv_nsec) / 1000000L;
}

void dormir_ms(int ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR);
}

int log_abrir(const char *ruta)
{
    ms_desde_inicio(); /* fija el reloj antes de crear procesos o hilos */
    fd_log = open(ruta, O_WRONLY | O_CREAT | O_APPEND, 0644);
    return fd_log < 0 ? -1 : 0;
}

void log_cerrar(void)
{
    if (fd_log >= 0)
        close(fd_log);
    fd_log = -1;
}

static void escribir_todo(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, p, n);

        if (w < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        p += w;
        n -= (size_t)w;
    }
}

void log_bloque(const char *texto, size_t n)
{
    escribir_todo(STDOUT_FILENO, texto, n);
    if (fd_log >= 0)
        escribir_todo(fd_log, texto, n);
}

void log_evento(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n = snprintf(buf, sizeof buf, "[%6ld ms] ", ms_desde_inicio());
    size_t cap = sizeof buf - (size_t)n - 1; /* deja 1 byte para el '\n' */
    int m;

    va_start(ap, fmt);
    m = vsnprintf(buf + n, cap, fmt, ap);
    va_end(ap);
    if (m < 0)
        m = 0;
    if ((size_t)m > cap - 1)
        m = (int)cap - 1;                    /* mensaje truncado */
    n += m;
    buf[n++] = '\n';
    log_bloque(buf, (size_t)n);
}