/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
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

void sieve_extprograms_plugin_load
(struct sieve_instance *svinst, void **context)
{
	struct _plugin_context *pctx = i_new(struct _plugin_context, 1);

	pctx->ext_pipe = sieve_extension_register
		(svinst, &pipe_extension, FALSE);
	pctx->ext_filter = sieve_extension_register
		(svinst, &filter_extension, FALSE);
	pctx->ext_execute = sieve_extension_register
		(svinst, &execute_extension, FALSE);

	if ( svinst->debug ) {
		sieve_sys_debug(svinst, "Sieve Extprograms plugin for %s version %s loaded",
			PIGEONHOLE_NAME, PIGEONHOLE_VERSION);
	}

	*context = (void *)pctx;
}

void sieve_extprograms_plugin_unload
(struct sieve_instance *svinst ATTR_UNUSED, void *context)
{
	struct _plugin_context *pctx = (struct _plugin_context *)context;

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
