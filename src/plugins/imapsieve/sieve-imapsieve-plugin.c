/* Copyright (c) 2016 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-extensions.h"

#include "ext-imapsieve-common.h"

#include "sieve-imapsieve-plugin.h"

/*
 * Sieve plugin interface
 */

const char *sieve_imapsieve_plugin_version = PIGEONHOLE_ABI_VERSION;

void sieve_imapsieve_plugin_load
(struct sieve_instance *svinst, void **context)
{
	const struct sieve_extension *ext;

	ext = sieve_extension_register
		(svinst, &imapsieve_extension_dummy, TRUE);

	if ( svinst->debug ) {
		sieve_sys_debug(svinst,
			"Sieve imapsieve plugin for %s version %s loaded",
			PIGEONHOLE_NAME, PIGEONHOLE_VERSION_FULL);
	}

	*context = (void *)ext;
}

void sieve_imapsieve_plugin_unload
(struct sieve_instance *svinst ATTR_UNUSED, void *context)
{
	const struct sieve_extension *ext =
		(const struct sieve_extension *)context;

	sieve_extension_unregister(ext);
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
