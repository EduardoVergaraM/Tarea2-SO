#ifndef MATCHMAKER_H
#define MATCHMAKER_H

#include "config.h"

/* Recibe n jugadores, los empareja y lanza una mesa por par*/
int matchmaker_ejecutar(const Config *cfg, int n_jugadores, int max_mesas);

#endif