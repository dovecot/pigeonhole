#ifndef DOVEADM_SIEVE_PLUGIN_H
#define DOVEADM_SIEVE_PLUGIN_H

/*
 * Plugin interface
 */

void doveadm_sieve_plugin_init(struct module *module);
void doveadm_sieve_plugin_deinit(void);

/*
 * Replication
 */

void doveadm_sieve_sync_init(struct module *module);

#endif
