/* Copyright (c) 2016-2018 Pigeonhole authors, see the included COPYING file
 */

#include "sieve.h"
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

int sieve_imapsieve_plugin_load(struct sieve_instance *svinst, void **context)
{
	const struct sieve_extension *ext_imapsieve;
	const struct sieve_extension *ext_vnd_imapsieve;
	struct _plugin_context *pctx;

	if (sieve_extension_register(svinst, &imapsieve_extension_dummy,
				     TRUE, &ext_imapsieve) < 0)
		return -1;
	if (sieve_extension_register(svinst, &vnd_imapsieve_extension_dummy,
				     TRUE, &ext_vnd_imapsieve) < 0)
		return -1;

	pctx = i_new(struct _plugin_context, 1);
	pctx->ext_imapsieve = ext_imapsieve;
	pctx->ext_vnd_imapsieve = ext_vnd_imapsieve;

	e_debug(sieve_get_event(svinst),
		"Sieve imapsieve plugin for %s version %s loaded",
		PIGEONHOLE_NAME, PIGEONHOLE_VERSION_FULL);

	*context = pctx;
	return 0;
}

void sieve_imapsieve_plugin_unload(struct sieve_instance *svinst ATTR_UNUSED,
				   void *context)
{
	struct _plugin_context *pctx = context;

	sieve_extension_unregister(pctx->ext_imapsieve);
	sieve_extension_unregister(pctx->ext_vnd_imapsieve);
	i_free(pctx);
}

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
