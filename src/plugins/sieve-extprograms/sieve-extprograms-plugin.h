#ifndef SIEVE_EXTPROGRAMS_PLUGIN_H
#define SIEVE_EXTPROGRAMS_PLUGIN_H

/*
 * Plugin interface
 */

int sieve_extprograms_plugin_load(struct sieve_instance *svinst,
				  void **context);
void sieve_extprograms_plugin_unload(struct sieve_instance *svinst,
				     void *context);

/*
 * Module interface
 */

void sieve_extprograms_plugin_init(void);
void sieve_extprograms_plugin_deinit(void);

#endif
