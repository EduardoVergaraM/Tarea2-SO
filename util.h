#ifndef UTIL_H
#define UTIL_H

<<<<<<< HEAD
/* Duerme ms milisegundos */
void dormir_ms(int ms);

/* Ms desde el primer uso */
long ms_desde_inicio(void);

=======
#include <stddef.h>

/* Duerme ms milisegundos */
void dormir_ms(int ms);

/* Ms desde el primer uso (seguro entre hilos) */
long ms_desde_inicio(void);

/* Abre el archivo de log (modo append). 0 = ok, -1 = error. Sin llamarla, solo se escribe en pantalla */
int  log_abrir(const char *ruta);
void log_cerrar(void);

/* Escribe un bloque (una o varias líneas) con un solo write() por destino: no se mezcla entre procesos */
void log_bloque(const char *texto, size_t n);

>>>>>>> 1ed2762 (Agrega módulo del tablero y log atómico)
/* Imprime una línea con el tiempo transcurrido */
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void log_evento(const char *fmt, ...);

#endif /* UTIL_H */