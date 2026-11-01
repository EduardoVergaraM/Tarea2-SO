# Torneo de Komikan Concurrente

Tarea 2 de Sistemas Operativos. Simulación de un torneo de Komikan en tiempo real: un Matchmaker empareja jugadores simulados y crea un proceso por partida (mesa), y dentro de cada mesa las fichas compiten como hilos por las casillas del tablero.

## Compilar y ejecutar

```
gcc -Wall -Wextra -std=c17 tarea1.c config.c -o torneo
./torneo N M [config.txt]
```

N es la cantidad de jugadores, M el máximo de mesas simultáneas y `config.txt` es un archivo opcional que puede configurarse previamente para determinar parámetros del juego.

## Cómo configurar `config.txt`

El archivo `config.txt` tiene 6 parámetros modificables, de estos, 5 se encuentran representados en una línea como un par clave-valor, mientras que el tablero se encuentra representado como un bloque. Lo que sigue a un `#` es un comentario y las líneas vacías se ignoran.  

| Clave | Qué define | Por defecto |
|---|---|---|
| `retardo_min` | Retardo mínimo entre movimientos de una ficha, en ms (entero de 1 a 60000) | 50 |
| `retardo_max` | Retardo máximo, en ms (no puede ser menor que `retardo_min`) | 300 |
| `semilla` | Con la misma semilla, cada ficha repite la misma secuencia de retardos (el orden de los movimientos entre fichas puede variar)| 42 |
| `log` | Archivo donde se registran las partidas | `torneo.log` |
| `falla` | Falla simulada de un jugador: `ninguna`, o `mesa,rol,movimiento`. Mesa, indica el número de mesa que va a fallar, rol indica quien presentará la falla (`puma` o `perros`) y movimiento indica desde que jugada se provocará la falla.| `ninguna` |
| `tablero` | Disposición inicial de las fichas (bloque, ver más abajo) | Puma en el trono y 12 perros en las últimas 12 casillas |



### Bloque `tablero`

Empieza con una línea `tablero` y termina con `fin_tablero`. Dentro van las 28 casillas en orden: el trono, los 2 nodos del triángulo y la parte baja del tablero (cuadrado de 5×5). Se usa `.` para una casilla vacía, `P` para el Puma y `D` para un perro. Debe haber exactamente 1 Puma y entre 1 y 12 perros. Los espacios y saltos de línea se ignoran.

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
