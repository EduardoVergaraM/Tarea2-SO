/* mesa.c: MESA TEMPORAL, simula una partida con resultado falso */
#define _POSIX_C_SOURCE 200809L
#include "mesa.h"
#include "util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

const char *texto_rol(Rol r)
{
    switch (r) {
    case ROL_PUMA:   return "Puma";
    case ROL_PERROS: return "Perros";
    default:         return "nadie";
    }
}

const char *texto_motivo(Motivo m)
{
    switch (m) {
    case MOTIVO_CAPTURAS:      return "capturas";
    case MOTIVO_ACORRALAMIENTO: return "acorralamiento";
    case MOTIVO_ABANDONO:      return "abandono";
    case MOTIVO_INTERRUPCION:  return "interrupción";
    default:                   return "ninguno";
    }
}

int mesa_ejecutar(const Config *cfg, int mesa_id, int id_puma, int id_perros,
                  int fd_resultado)
{
    /* Generador propio de la mesa */
    unsigned int rng = cfg->semilla ^ (0x9E3779B9u * (unsigned int)mesa_id);
    unsigned int rango = (unsigned int)(cfg->retardo_max_ms - cfg->retardo_min_ms) + 1u;
    int n_mov = 5 + (int)(rand_r(&rng) % 11u); /* movimientos simulados (5 a 15) */
    bool hay_falla = (cfg->falla.mesa == mesa_id);
    ResultadoMesa r = { ROL_NINGUNO, MOTIVO_NINGUNO, 0, 0 };
    ssize_t w;

    log_evento("[mesa %d] comienza: Puma = jugador %d, Perros = jugador %d", mesa_id, id_puma, id_perros);

    for (int m = 1; m <= n_mov; m++) {
        dormir_ms(cfg->retardo_min_ms + (int)(rand_r(&rng) % rango));
        r.movimientos = m;
        if (hay_falla && m == cfg->falla.movimiento) {
            log_evento("[mesa %d] el jugador %s falla tras %d movimientos", mesa_id, texto_rol(cfg->falla.rol), m);
            r.motivo = MOTIVO_ABANDONO;
            r.ganador = (cfg->falla.rol == ROL_PUMA) ? ROL_PERROS : ROL_PUMA;
            break;
        }
    }

    /* Sin falla: resultado al azar */
    if (r.motivo == MOTIVO_NINGUNO) {
        if (rand_r(&rng) % 2u == 0) {
            r.ganador = ROL_PUMA;
            r.motivo = MOTIVO_CAPTURAS;
            r.capturas = 6;
        } else {
            r.ganador = ROL_PERROS;
            r.motivo = MOTIVO_ACORRALAMIENTO;
            r.capturas = (int)(rand_r(&rng) % 6u);
        }
    }

    log_evento("[mesa %d] termina: gana %s por %s", mesa_id, texto_rol(r.ganador), texto_motivo(r.motivo));

    w = write(fd_resultado, &r, sizeof r);

    while (w < 0 && errno == EINTR) {
        w = write(fd_resultado, &r, sizeof r);
    }
    return (w == (ssize_t)sizeof r) ? 0 : -1;
}