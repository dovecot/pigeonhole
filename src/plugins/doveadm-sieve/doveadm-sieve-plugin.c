/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "doveadm-mail.h"

#include "sieve.h"

#include "doveadm-sieve-cmd.h"
#include "doveadm-sieve-plugin.h"

const char *doveadm_sieve_plugin_version = DOVECOT_ABI_VERSION;

void doveadm_sieve_plugin_init(struct module *module)
{
	doveadm_sieve_sync_init(module);
	doveadm_sieve_cmds_init();
}

void doveadm_sieve_plugin_deinit(void)
{
	/* the hooks array is freed already */
	/*mail_storage_hooks_remove(&doveadm_sieve_mail_storage_hooks);*/
}
