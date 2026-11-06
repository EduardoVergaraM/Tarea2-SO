# Torneo de Komikan Concurrente

Tarea 2 de Sistemas Operativos. Simula el Komikan, un juego tradicional de estrategia mapuche, el cual enfrenta un puma con un grupo de 12 perros. El puma intenta capturarlos, mientras que los perros deben rodearlo e impedir que se siga moviendo. El tablero tiene 28 casillas conectadas y en la disposición inicial de este proyecto participan 1 puma y 12 perros.

Las fichas pueden avanzar a una casilla vecina que esté libre. Además, el puma puede capturar a un perro saltando sobre él en línea recta, siempre que la casilla de llegada esté desocupada. Los perros no realizan capturas porque su objetivo es acorralar o bloquear al puma. Las condiciones de victoria implementadas son las siguientes:
- Gana el puma si consigue capturar 6 o más perros.
- Ganan los perros si acorralan al puma y lo dejan sin movimientos posibles.

Este proyecto simula un torneo de Komikan en tiempo real, sin turnos fijos: un Matchmaker empareja jugadores simulados y crea un proceso por partida (mesa). Dentro de cada mesa, las fichas se ejecutan como hilos que compiten por las casillas del tablero, mientras un juez revisa el estado del juego y determina cuándo termina la partida.

## Compilar y ejecutar

```
gcc -Wall -Wextra -std=c17 -pthread tarea1.c config.c matchmaker.c mesa.c tablero.c util.c -o torneo
```

```
./torneo N M config_ejemplo.txt
```

N es la cantidad de jugadores, M el máximo de mesas simultáneas y `config.txt` es un archivo opcional que puede configurarse previamente para determinar parámetros del juego. A continuación, un ejemplo de cómo se puede modificar la ejecución del programa:

```
./torneo 7 2 config_ejemplo.txt
```

## Cómo configurar `config.txt`

El archivo `config.txt` tiene 6 parámetros modificables; de estos, 5 se encuentran representados en una línea como un par clave-valor, mientras que el tablero se encuentra representado como un bloque. Lo que sigue a un `#` es un comentario y las líneas vacías se ignoran.

| Clave | Qué define | Por defecto |
|---|---|---|
| `retardo_min` | Retardo mínimo entre movimientos de una ficha, en ms (entero de 1 a 60000) | 50 |
| `retardo_max` | Retardo máximo, en ms (no puede ser menor que `retardo_min`) | 300 |
| `semilla` | Con la misma semilla, cada ficha repite la misma secuencia de retardos (el orden de los movimientos entre fichas puede variar) | 42 |
| `log` | Archivo donde se registran las partidas | `torneo.log` |
| `falla` | Falla simulada de un jugador: `ninguna`, o `mesa,rol,movimiento`. Mesa indica el número de mesa que va a fallar, rol indica quién presentará la falla (`puma` o `perros`) y movimiento indica desde qué jugada se provocará la falla. | `ninguna` |
| `tablero` | Disposición inicial de las fichas (bloque, ver más abajo) | puma en el trono y 12 perros en las últimas 12 casillas |

### Bloque `tablero`

Empieza con una línea `tablero` y termina con `fin_tablero`. Dentro van las 28 casillas en orden: el trono, los 2 nodos del triángulo y la parte baja del tablero (cuadrado de 5×5). Se usa `.` para una casilla vacía, `P` para el puma y `D` para un perro. Debe haber exactamente 1 puma y 12 perros. Los espacios y saltos de línea se ignoran.

### Ejemplo

```
# Retardos entre movimientos (ms)
retardo_min = 100
retardo_max = 200
semilla = 7
falla = 2,perros,10

tablero
P
..
.....
.....
...DD
DDDDD
DDDDD
fin_tablero
```

### Valores inválidos

Si una clave falta, está vacía o es inválida, se usa su valor por defecto y se avisa por pantalla. Los dos retardos se validan juntos, es decir, si uno de los valores ingresados es incorrecto, se aplican los valores por defecto a ambos parámetros. El tablero se acepta completo o se reemplaza por el estándar.

## Archivos del proyecto

| Archivo | Qué contiene |
|---|---|
| `tarea1.c` | Punto de entrada del programa: recibe los argumentos, carga la configuración y ejecuta el Matchmaker. |
| `config.c` | Lee y valida el archivo de configuración, y aplica los valores por defecto cuando corresponde. |
| `config.h` | Define los parámetros, tipos y funciones utilizados para configurar el torneo. |
| `matchmaker.c` | Simula la llegada de jugadores, forma las parejas, administra las mesas simultáneas y gestiona la interrupción con `Ctrl+C`. |
| `matchmaker.h` | Declara la función que inicia y administra el torneo. |
| `mesa.c` | Ejecuta una partida al crear los hilos de las fichas, coordinar al juez y preparar el resultado final. |
| `mesa.h` | Define los resultados y motivos de término de las partidas, además de las funciones de la mesa. |
| `tablero.c` | Implementa las reglas de movimiento, las capturas, las fotografías del tablero y la sincronización de las casillas. |
| `tablero.h` | Define las estructuras, constantes y funciones que utiliza el tablero. |
| `util.c` | Contiene funciones auxiliares para los tiempos de espera y los mensajes de la terminal y del log. |
| `util.h` | Declara las funciones auxiliares usadas en distintas partes del programa. |
| `test_tablero.c` | Incluye pruebas de las reglas del tablero y de los movimientos simultáneos entre hilos. |
| `config_ejemplo.txt` | Archivo de ejemplo con los parámetros y la disposición inicial del tablero. |
| `README.md` | Explica el funcionamiento general, la ejecución y la configuración del proyecto. |
