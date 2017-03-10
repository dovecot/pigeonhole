/* Copyright (c) 2016-2017 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-extensions.h"

#include "ext-imapsieve-common.h"

#include "sieve-imapsieve-plugin.h"

/*
 * Sieve plugin interface
 */

struct _plugin_context {
	const struct sieve_extension *ext_imapsieve;
	const struct sieve_extension *ext_vnd_imapsieve;
};

const char *sieve_imapsieve_plugin_version = PIGEONHOLE_ABI_VERSION;

void sieve_imapsieve_plugin_load
(struct sieve_instance *svinst, void **context)
{
	struct _plugin_context *pctx = i_new(struct _plugin_context, 1);

	pctx->ext_imapsieve = sieve_extension_register
		(svinst, &imapsieve_extension_dummy, TRUE);
	pctx->ext_vnd_imapsieve = sieve_extension_register
		(svinst, &vnd_imapsieve_extension_dummy, TRUE);

	if ( svinst->debug ) {
		sieve_sys_debug(svinst,
			"Sieve imapsieve plugin for %s version %s loaded",
			PIGEONHOLE_NAME, PIGEONHOLE_VERSION_FULL);
	}

	*context = (void *)pctx;
}

void sieve_imapsieve_plugin_unload
(struct sieve_instance *svinst ATTR_UNUSED, void *context)
{
	struct _plugin_context *pctx = (struct _plugin_context *)context;

	sieve_extension_unregister(pctx->ext_imapsieve);
	sieve_extension_unregister(pctx->ext_vnd_imapsieve);

	i_free(pctx);}

/*
 * Module interface
 */

void sieve_imapsieve_plugin_init(void)
{
	/* Nothing */
}

void sieve_imapsieve_plugin_deinit(void)
{
	/* Nothing */
}
