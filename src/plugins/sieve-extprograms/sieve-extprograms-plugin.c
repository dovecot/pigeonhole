/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-extensions.h"

#include "sieve-extprograms-common.h"
#include "sieve-extprograms-plugin.h"

/*
 * Sieve plugin interface
 */

struct _plugin_context {
	const struct sieve_extension *ext_pipe;
	const struct sieve_extension *ext_filter;
	const struct sieve_extension *ext_execute;
};

const char *sieve_extprograms_plugin_version = PIGEONHOLE_ABI_VERSION;

int sieve_extprograms_plugin_load(struct sieve_instance *svinst,
				  void **context)
{
	const struct sieve_extension *ext_pipe;
	const struct sieve_extension *ext_filter;
	const struct sieve_extension *ext_execute;
	struct _plugin_context *pctx;

	if (sieve_extension_register(svinst, &sieve_ext_vnd_pipe, FALSE,
				     &ext_pipe) < 0)
		return -1;
	if (sieve_extension_register(svinst, &sieve_ext_vnd_filter, FALSE,
				     &ext_filter) < 0)
		return -1;
	if (sieve_extension_register(svinst, &sieve_ext_vnd_execute, FALSE,
				     &ext_execute) < 0)
		return -1;

	pctx = i_new(struct _plugin_context, 1);
	pctx->ext_pipe = ext_pipe;
	pctx->ext_filter = ext_filter;
	pctx->ext_execute = ext_execute;

	if (svinst->debug) {
		e_debug(svinst->event,
			"Sieve Extprograms plugin for %s version %s loaded",
			PIGEONHOLE_NAME, PIGEONHOLE_VERSION_FULL);
	}

	*context = pctx;
	return 0;
}

void sieve_extprograms_plugin_unload(struct sieve_instance *svinst ATTR_UNUSED,
				     void *context)
{
	struct _plugin_context *pctx = context;

	sieve_extension_unregister(pctx->ext_pipe);
	sieve_extension_unregister(pctx->ext_filter);
	sieve_extension_unregister(pctx->ext_execute);
	i_free(pctx);
}

/*
 * Module interface
 */

void sieve_extprograms_plugin_init(void)
{
	/* Nothing */
}

void sieve_extprograms_plugin_deinit(void)
{
	/* Nothing */
}
