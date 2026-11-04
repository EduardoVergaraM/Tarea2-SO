/* Programa principal: carga la configuración y lanza el Matchmaker */
#include "config.h"
#include "matchmaker.h"
#include "util.h"

int main(int argc, char **argv)
{
    ParametrosEjecucion p;
    Config cfg;

    setvbuf(stdout, NULL, _IOLBF, 0); /* salida por líneas */

    if (config_parsear_args(argc, argv, &p) != 0)
        return 1;

    config_cargar(&cfg, p.ruta_config);
    config_imprimir(stdout, &cfg);

    if (log_abrir(cfg.ruta_log) != 0)
        fprintf(stderr, "no se pudo abrir el log\n");

    int rc = matchmaker_ejecutar(&cfg, p.n, p.m);
    log_cerrar();
    return rc;
}