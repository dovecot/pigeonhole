/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#ifndef __DOVEADM_SIEVE_PLUGIN_H
#define __DOVEADM_SIEVE_PLUGIN_H

/*
 * Plugin interface
 */

void doveadm_sieve_plugin_init(struct module *module);
void doveadm_sieve_plugin_deinit(void);

/*
 * Replication
 */

void doveadm_sieve_sync_init(struct module *module);

#endif /* __DOVEADM_SIEVE_PLUGIN_H */
