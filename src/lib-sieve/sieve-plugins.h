#ifndef SIEVE_PLUGINS_H
#define SIEVE_PLUGINS_H

#include "sieve-common.h"

int sieve_plugins_load(struct sieve_instance *svinst, const char *path,
		       const char *plugins);
void sieve_plugins_unload(struct sieve_instance *svinst);

#endif
