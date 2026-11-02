#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>

/* Tablero: 28 casillas. 0 = trono, 1-2 = triángulo, 3-27 = grilla 5x5 por filas (3 + 5*fila + col) */
#define NUM_CASILLAS 28
#define NUM_PERROS   12

/* Valores por defecto */
#define RETARDO_MIN_DEFECTO 50
#define RETARDO_MAX_DEFECTO 300
#define RETARDO_TOPE_MS     60000
#define SEMILLA_DEFECTO     42u
#define LOG_DEFECTO         "torneo.log"

#define LARGO_RUTA    256
#define LARGO_MENSAJE 160
#define MAX_MENSAJES  32

/* Contenido de una casilla */
typedef enum { CELDA_VACIA = 0, CELDA_PUMA, CELDA_PERRO } Celda;

/* Jugador que puede fallar */
typedef enum { ROL_NINGUNO = 0, ROL_PUMA, ROL_PERROS } Rol;

/* Falla simulada: mesa, rol y movimiento en que falla. mesa = 0: sin falla */
typedef struct {
    int mesa;
    Rol rol;
    int movimiento;
} FallaSimulada;

/* Configuración efectiva del torneo */
typedef struct {
    int           retardo_min_ms;
    int           retardo_max_ms;
    unsigned int  semilla;
    Celda         disposicion[NUM_CASILLAS];
    FallaSimulada falla;
    char          ruta_log[LARGO_RUTA];

    /* Avisos de la carga, para volcarlos al log */
    int  n_mensajes;
    char mensajes[MAX_MENSAJES][LARGO_MENSAJE];
} Config;

/* Argumentos de la línea de comandos: ./torneo N M [config.txt] */
typedef struct {
    int         n;
    int         m;
    const char *ruta_config; /* NULL si no se entregó */
} ParametrosEjecucion;

/* Deja cfg con los valores por defecto */
void config_defecto(Config *cfg);

/* Carga el config desde ruta (NULL = sin archivo). Nunca falla: avisa por stderr */
void config_cargar(Config *cfg, const char *ruta);

/* Valida argv: 0 si está bien, -1 si no (imprime el uso) */
int config_parsear_args(int argc, char **argv, ParametrosEjecucion *p);

/* Imprime la configuración efectiva */
void config_imprimir(FILE *out, const Config *cfg);

/* Escribe en out los avisos de la carga (para el log) */
void config_volcar_mensajes(FILE *out, const Config *cfg);

#endif /* CONFIG_H */