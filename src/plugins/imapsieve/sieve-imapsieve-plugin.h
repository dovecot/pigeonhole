/* Copyright (c) 2016-2017 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_IMAPSIEVE_PLUGIN_H
#define __SIEVE_IMAPSIEVE_PLUGIN_H

/*
 * Plugin interface
 */

void sieve_imapsieve_plugin_load
	(struct sieve_instance *svinst, void **context);
void sieve_imapsieve_plugin_unload
	(struct sieve_instance *svinst, void *context);

/*
 * Module interface
 */

void sieve_imapsieve_plugin_init(void);
void sieve_imapsieve_plugin_deinit(void);

#endif /* __SIEVE_IMAPSIEVE_PLUGIN_H */
