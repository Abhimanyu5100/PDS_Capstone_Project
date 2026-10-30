/* engine.h - internals shared between the engine and its admin scenarios. */
#ifndef PLUTUS_ENGINE_H
#define PLUTUS_ENGINE_H

#include "../common/util.h"

/* Offline demos that need no live bank state (scenarios.c). Returns false if
 * the name is unknown. */
bool scenario_offline(const char *name, char **args, int nargs, sbuf_t *out);
void scenario_list(sbuf_t *out);

#endif
