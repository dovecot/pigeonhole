/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_EXTPROGRAMS_PLUGIN_H
#define __SIEVE_EXTPROGRAMS_PLUGIN_H

/*
 * Plugin interface
 */

void sieve_extprograms_plugin_load
	(struct sieve_instance *svinst, void **context);
void sieve_extprograms_plugin_unload
	(struct sieve_instance *svinst, void *context);

/*
 * Module interface
 */

void sieve_extprograms_plugin_init(void);
void sieve_extprograms_plugin_deinit(void);

#endif /* __SIEVE_EXTPROGRAMS_PLUGIN_H */
