#ifndef UTIL_H
#define UTIL_H

/* Duerme ms milisegundos */
void dormir_ms(int ms);

/* Ms desde el primer uso */
long ms_desde_inicio(void);

/* Imprime una línea con el tiempo transcurrido */
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void log_evento(const char *fmt, ...);

#endif /* UTIL_H */