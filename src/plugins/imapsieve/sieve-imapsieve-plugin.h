#ifndef SIEVE_IMAPSIEVE_PLUGIN_H
#define SIEVE_IMAPSIEVE_PLUGIN_H

/*
 * Plugin interface
 */

int sieve_imapsieve_plugin_load(struct sieve_instance *svinst,
				void **context);
void sieve_imapsieve_plugin_unload(struct sieve_instance *svinst,
				   void *context);

/*
 * Module interface
 */

void sieve_imapsieve_plugin_init(void);
void sieve_imapsieve_plugin_deinit(void);

#endif
