#ifndef MESA_H
#define MESA_H

#include "config.h"

/* Por qué terminó la partida */
typedef enum {
    MOTIVO_NINGUNO = 0,
    MOTIVO_CAPTURAS,
    MOTIVO_ACORRALAMIENTO,
    MOTIVO_ABANDONO,
    MOTIVO_INTERRUPCION,
    MOTIVO_TIEMPO            /* se agotó el tiempo límite de la partida */
} Motivo;

/* Resultado que la mesa escribe en su pipe */
typedef struct {
    Rol    ganador;
    Motivo motivo;
    int    capturas;
    int    movimientos;
} ResultadoMesa;

/* Juega una partida y escribe el resultado en fd_resultado. 0 = ok, -1 = error */
int mesa_ejecutar(const Config *cfg, int mesa_id, int id_puma, int id_perros, int fd_resultado);

/* Textos para mensajes */
const char *texto_rol(Rol r);
const char *texto_motivo(Motivo m);

#endif 
