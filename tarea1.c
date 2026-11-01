/* Programa principal: por ahora solo carga e imprime la configuración */
#include "config.h"

int main(int argc, char **argv)
{
    ParametrosEjecucion p;
    Config cfg;

    if (config_parsear_args(argc, argv, &p) != 0)
        return 1;

    config_cargar(&cfg, p.ruta_config);
    config_imprimir(stdout, &cfg);
    return 0;
}