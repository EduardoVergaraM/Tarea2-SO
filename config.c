/* config.c: lectura y validación de config.txt y de los argumentos */
#define _POSIX_C_SOURCE 200809L
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define LARGO_LINEA 256

_Static_assert(NUM_CASILLAS > NUM_PERROS, "NUM_CASILLAS debe poder alojar al Puma y a los perros");

/* Claves del config */
enum {
    K_RMIN    = 1u << 0,
    K_RMAX    = 1u << 1,
    K_SEMILLA = 1u << 2,
    K_LOG     = 1u << 3,
    K_FALLA   = 1u << 4,
    K_TABLERO = 1u << 5
};

static const struct {
    unsigned    bit;
    const char *nombre;
} CLAVES[] = {
    { K_RMIN,    "retardo_min" },
    { K_RMAX,    "retardo_max" },
    { K_SEMILLA, "semilla"     },
    { K_LOG,     "log"         },
    { K_FALLA,   "falla"       },
    { K_TABLERO, "tablero"     },
};
#define N_CLAVES (sizeof CLAVES / sizeof CLAVES[0])

/* Utilidades */

/* Avisa por stderr y guarda el mensaje */
#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
static void registrar(Config *cfg, const char *nivel, const char *fmt, ...)
{
    char texto[LARGO_MENSAJE];
    va_list ap;
    int k = snprintf(texto, sizeof texto, "[config] %s: ", nivel);

    va_start(ap, fmt);
    vsnprintf(texto + k, sizeof texto - (size_t)k, fmt, ap);
    va_end(ap);

    fprintf(stderr, "%s\n", texto);
    if (cfg->n_mensajes < MAX_MENSAJES) {
        memcpy(cfg->mensajes[cfg->n_mensajes], texto, sizeof texto);
        cfg->n_mensajes++;
    }
}

/* Quitar los espacios de los extremos */
static char *recortar(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    return s;
}

/* Lee una línea. Si no cabe, la descarta y marca truncada. Devuelve 0 en EOF */
static int leer_linea(FILE *f, char *buf, size_t n, bool *truncada)
{
    *truncada = false;
    if (fgets(buf, (int)n, f) == NULL)
        return 0;
    if (strchr(buf, '\n') == NULL && !feof(f)) {
        int c;
        *truncada = true;
        while ((c = fgetc(f)) != '\n' && c != EOF)
            ;
    }
    return 1;
}

/* Entero dentro de [min, max] */
static bool parsear_entero(const char *s, long min, long max, long *out)
{
    char *fin;
    long v;

    if (*s == '\0')
        return false;
    errno = 0;
    v = strtol(s, &fin, 10);
    if (errno != 0 || *fin != '\0' || v < min || v > max)
        return false;
    *out = v;
    return true;
}

/* Entero sin signo para la semilla */
static bool parsear_semilla(const char *s, unsigned int *out)
{
    char *fin;
    unsigned long v;

    if (*s == '\0' || *s == '-') /* strtoul acepta negativos */
        return false;
    errno = 0;
    v = strtoul(s, &fin, 10);
    if (errno != 0 || *fin != '\0' || v > UINT_MAX)
        return false;
    *out = (unsigned int)v;
    return true;
}

/* "ninguna" o "mesa,rol,movimiento" (rol: puma | perros) */
static bool parsear_falla(const char *valor, FallaSimulada *out)
{
    char copia[LARGO_LINEA];
    char *c1, *c2;
    long mesa, mov;
    Rol rol;

    if (strcmp(valor, "ninguna") == 0) {
        out->mesa = 0;
        out->rol = ROL_NINGUNO;
        out->movimiento = 0;
        return true;
    }

    snprintf(copia, sizeof copia, "%s", valor);
    c1 = strchr(copia, ',');
    if (c1 == NULL)
        return false;
    c2 = strchr(c1 + 1, ',');
    if (c2 == NULL || strchr(c2 + 1, ',') != NULL)
        return false;
    *c1 = '\0';
    *c2 = '\0';

    if (!parsear_entero(recortar(copia), 1, INT_MAX, &mesa) ||
        !parsear_entero(recortar(c2 + 1), 1, INT_MAX, &mov))
        return false;

    {
        const char *s_rol = recortar(c1 + 1);
        if (strcmp(s_rol, "puma") == 0)
            rol = ROL_PUMA;
        else if (strcmp(s_rol, "perros") == 0)
            rol = ROL_PERROS;
        else
            return false;
    }

    out->mesa = (int)mesa;
    out->rol = rol;
    out->movimiento = (int)mov;
    return true;
}

/* Lee el bloque hasta fin_tablero ('.', 'P', 'D'). Solo se copia si es válido */
static void leer_tablero(FILE *f, Config *cfg, Celda destino[NUM_CASILLAS], int *nlinea)
{
    Celda tmp[NUM_CASILLAS] = { CELDA_VACIA };
    char linea[LARGO_LINEA];
    char motivo[80] = "";
    bool trunc, cerrado = false;
    int n = 0, pumas = 0, perros = 0;

    while (leer_linea(f, linea, sizeof linea, &trunc)) {
        char *h, *s;

        (*nlinea)++;
        if (trunc && motivo[0] == '\0')
            snprintf(motivo, sizeof motivo, "línea %d demasiado larga", *nlinea);

        h = strchr(linea, '#');
        if (h != NULL)
            *h = '\0';
        s = recortar(linea);

        if (strcmp(s, "fin_tablero") == 0) {
            cerrado = true;
            break;
        }

        for (; *s != '\0'; s++) {
            Celda c;

            if (isspace((unsigned char)*s))
                continue;
            switch (toupper((unsigned char)*s)) {
            case '.': c = CELDA_VACIA; break;
            case 'P': c = CELDA_PUMA;  pumas++;  break;
            case 'D': c = CELDA_PERRO; perros++; break;
            default:
                if (motivo[0] == '\0')
                    snprintf(motivo, sizeof motivo, "carácter '%c' no válido en línea %d", *s, *nlinea);
                continue;
            }
            if (n < NUM_CASILLAS)
                tmp[n] = c;
            n++;
        }
    }

    if (!cerrado && motivo[0] == '\0')
        snprintf(motivo, sizeof motivo, "falta 'fin_tablero'");
    if (motivo[0] == '\0' && n != NUM_CASILLAS)
        snprintf(motivo, sizeof motivo, "se esperaban %d casillas y hay %d",NUM_CASILLAS, n);
    if (motivo[0] == '\0' && pumas != 1)
        snprintf(motivo, sizeof motivo, "debe haber exactamente 1 Puma (hay %d)", pumas);
    if (motivo[0] == '\0' && perros != NUM_PERROS)
        snprintf(motivo, sizeof motivo, "debe haber exactamente %d perros (hay %d)", NUM_PERROS, perros);
    if (motivo[0] != '\0') {
        registrar(cfg, "aviso", "bloque 'tablero' inválido (%s); se usa la disposición estándar", motivo);
        return;
    }
    memcpy(destino, tmp, sizeof tmp);
}

/* Funciones publicas  */

void config_defecto(Config *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->retardo_min_ms = RETARDO_MIN_DEFECTO;
    cfg->retardo_max_ms = RETARDO_MAX_DEFECTO;
    cfg->semilla = SEMILLA_DEFECTO;
    cfg->falla.mesa = 0;
    cfg->falla.rol = ROL_NINGUNO;
    cfg->falla.movimiento = 0;
    snprintf(cfg->ruta_log, sizeof cfg->ruta_log, "%s", LOG_DEFECTO);

    /* Disposición estándar tomando como supuesto que el puma se encuentra en el trono, perros en las últimas 12 casillas */
    for (int i = 0; i < NUM_CASILLAS; i++)
        cfg->disposicion[i] = CELDA_VACIA;
    cfg->disposicion[0] = CELDA_PUMA;
    for (int i = NUM_CASILLAS - NUM_PERROS; i < NUM_CASILLAS; i++)
        cfg->disposicion[i] = CELDA_PERRO;
}

/* Carga el config desde ruta; lo inválido o ausente queda con su valor por defecto */
void config_cargar(Config *cfg, const char *ruta)
{
    FILE *f;
    char linea[LARGO_LINEA];
    bool trunc;
    int nlinea = 0;
    unsigned visto = 0;
    long rmin = 0, rmax = 0;
    bool rmin_ok = false, rmax_ok = false;

    config_defecto(cfg);

    if (ruta == NULL) {
        registrar(cfg, "info", "sin archivo de configuración; se usan los valores por defecto");
        return;
    }
    f = fopen(ruta, "r");
    if (f == NULL) {
        registrar(cfg, "aviso", "no se pudo abrir '%s' (%s); se usan los valores por defecto", ruta, strerror(errno));
        return;
    }

    while (leer_linea(f, linea, sizeof linea, &trunc)) {
        char *h, *s, *eq, *clave, *valor;
        unsigned bit = 0;

        nlinea++;
        if (trunc) {
            registrar(cfg, "aviso", "línea %d demasiado larga; se ignora", nlinea);
            continue;
        }
        h = strchr(linea, '#');
        if (h != NULL)
            *h = '\0';
        s = recortar(linea);
        if (*s == '\0')
            continue;

        /* Bloque tablero */
        if (strcmp(s, "tablero") == 0 || strcmp(s, "tablero:") == 0) {
            if (visto & K_TABLERO) {
                Celda descarte[NUM_CASILLAS];
                registrar(cfg, "aviso", "línea %d: bloque 'tablero' repetido; se ignora", nlinea);
                leer_tablero(f, cfg, descarte, &nlinea);
            } else {
                visto |= K_TABLERO;
                leer_tablero(f, cfg, cfg->disposicion, &nlinea);
            }
            continue;
        }

        eq = strchr(s, '=');
        if (eq == NULL) {
            registrar(cfg, "aviso", "línea %d: se esperaba 'clave = valor' ('%s'); se ignora", nlinea, s);
            continue;
        }
        *eq = '\0';
        clave = recortar(s);
        valor = recortar(eq + 1);

        for (size_t i = 0; i < N_CLAVES; i++)
            if (strcmp(clave, CLAVES[i].nombre) == 0)
                bit = CLAVES[i].bit;
        if (bit == 0 || bit == K_TABLERO) {
            registrar(cfg, "aviso", "línea %d: clave desconocida o mal usada '%s'; se ignora", nlinea, clave);
            continue;
        }
        if (visto & bit) {
            registrar(cfg, "aviso", "línea %d: clave '%s' repetida; se ignora", nlinea, clave);
            continue;
        }
        visto |= bit;

        if (*valor == '\0') {
            registrar(cfg, "aviso", "línea %d: '%s' sin valor; se usa el valor por defecto", nlinea, clave);
            continue;
        }

        switch (bit) {
        case K_RMIN:
            if (parsear_entero(valor, 1, RETARDO_TOPE_MS, &rmin))
                rmin_ok = true;
            else
                registrar(cfg, "aviso", "línea %d: retardo_min inválido ('%s')", nlinea, valor);
            break;
        case K_RMAX:
            if (parsear_entero(valor, 1, RETARDO_TOPE_MS, &rmax))
                rmax_ok = true;
            else
                registrar(cfg, "aviso", "línea %d: retardo_max inválido ('%s')", nlinea, valor);
            break;
        case K_SEMILLA:
            if (!parsear_semilla(valor, &cfg->semilla))
                registrar(cfg, "aviso", "línea %d: semilla inválida ('%s'); se usa %u", nlinea, valor, SEMILLA_DEFECTO);
            break;
        case K_LOG:
            if (strlen(valor) < sizeof cfg->ruta_log)
                snprintf(cfg->ruta_log, sizeof cfg->ruta_log, "%s", valor);
            else
                registrar(cfg, "aviso", "línea %d: ruta de log demasiado larga; se usa '%s'", nlinea, LOG_DEFECTO);
            break;
        case K_FALLA:
            if (!parsear_falla(valor, &cfg->falla))
                registrar(cfg, "aviso", "línea %d: falla inválida ('%s'); se usa 'ninguna'", nlinea, valor);
            break;
        default:
            break;
        }
    }
    fclose(f);

    /* Los retardos se aceptan juntos */
    if (rmin_ok && rmax_ok && rmin <= rmax) {
        cfg->retardo_min_ms = (int)rmin;
        cfg->retardo_max_ms = (int)rmax;
    } else if (visto & (K_RMIN | K_RMAX)) {
        registrar(cfg, "aviso", "retardos incompletos, inválidos o con mínimo > máximo; " "se usan %d y %d ms por defecto", RETARDO_MIN_DEFECTO, RETARDO_MAX_DEFECTO);
    }

    /* Claves ausentes: solo informar */
    for (size_t i = 0; i < N_CLAVES; i++)
        if (!(visto & CLAVES[i].bit))
            registrar(cfg, "info", "'%s' no especificado; se usa el valor por defecto",
                      CLAVES[i].nombre);
}

/* Validar N, M y la ruta opcional del config */
int config_parsear_args(int argc, char **argv, ParametrosEjecucion *p)
{
    const char *prog = (argc > 0 && argv[0] != NULL) ? argv[0] : "torneo";
    long n = 0, m = 0;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Error: cantidad de argumentos incorrecta.\n");
    } else if (!parsear_entero(argv[1], 2, INT_MAX, &n)) {
        fprintf(stderr, "Error: N debe ser un entero >= 2 (recibido '%s').\n", argv[1]);
    } else if (!parsear_entero(argv[2], 1, INT_MAX, &m)) {
        fprintf(stderr, "Error: M debe ser un entero >= 1 (recibido '%s').\n", argv[2]);
    } else {
        p->n = (int)n;
        p->m = (int)m;
        p->ruta_config = (argc == 4) ? argv[3] : NULL;
        return 0;
    }
    fprintf(stderr, "Uso: %s N M [config.txt]\n", prog);
    return -1;
}

/* Letra de una casilla */
static char simbolo(Celda c)
{
    switch (c) {
    case CELDA_PUMA:  return 'P';
    case CELDA_PERRO: return 'D';
    default:          return '.';
    }
}

/* Imprimir la configuración efectiva */
void config_imprimir(FILE *out, const Config *cfg)
{
    fprintf(out, "[config] configuración efectiva:\n");
    fprintf(out, "  retardo_min = %d ms\n", cfg->retardo_min_ms);
    fprintf(out, "  retardo_max = %d ms\n", cfg->retardo_max_ms);
    fprintf(out, "  semilla     = %u\n", cfg->semilla);
    fprintf(out, "  log         = %s\n", cfg->ruta_log);
    if (cfg->falla.mesa == 0)
        fprintf(out, "  falla       = ninguna\n");
    else
        fprintf(out, "  falla       = mesa %d, %s, tras %d movimientos\n",
                cfg->falla.mesa,
                cfg->falla.rol == ROL_PUMA ? "puma" : "perros",
                cfg->falla.movimiento);
    fprintf(out, "  tablero     = ");
    for (int i = 0; i < NUM_CASILLAS; i++)
        fputc(simbolo(cfg->disposicion[i]), out);
    fputc('\n', out);
}

/* Escribe los avisos de la carga (para el log) */
void config_volcar_mensajes(FILE *out, const Config *cfg)
{
    for (int i = 0; i < cfg->n_mensajes; i++)
        fprintf(out, "%s\n", cfg->mensajes[i]);
}