/* util.c: espera en ms y log con tiempo */
#define _POSIX_C_SOURCE 200809L
#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static struct timespec inicio;
static int reloj_iniciado = 0;

/* Ms desde el primer uso, donde los hijos de fork lo heredan */
long ms_desde_inicio(void)
{
    struct timespec ahora;

    if (!reloj_iniciado) {
        clock_gettime(CLOCK_MONOTONIC, &inicio);
        reloj_iniciado = 1;
    }
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

void log_evento(const char *fmt, ...)
{
    va_list ap;

    flockfile(stdout); /* una línea completa por hilo */
    printf("[%6ld ms] ", ms_desde_inicio());
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    funlockfile(stdout);
}