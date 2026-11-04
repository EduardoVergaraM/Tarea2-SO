/* Programa principal: carga la configuración y lanza el Matchmaker */
#include "config.h"
#include "matchmaker.h"
<<<<<<< HEAD
=======
#include "util.h"
>>>>>>> 1ed2762 (Agrega módulo del tablero y log atómico)

int main(int argc, char **argv)
{
    ParametrosEjecucion p;
    Config cfg;

    setvbuf(stdout, NULL, _IOLBF, 0); /* salida por líneas */

    if (config_parsear_args(argc, argv, &p) != 0)
        return 1;

    config_cargar(&cfg, p.ruta_config);
    config_imprimir(stdout, &cfg);
<<<<<<< HEAD
    return matchmaker_ejecutar(&cfg, p.n, p.m);
=======

    if (log_abrir(cfg.ruta_log) != 0)
        fprintf(stderr, "no se pudo abrir el log\n");

    int rc = matchmaker_ejecutar(&cfg, p.n, p.m);
    log_cerrar();
    return rc;
>>>>>>> 1ed2762 (Agrega módulo del tablero y log atómico)
}