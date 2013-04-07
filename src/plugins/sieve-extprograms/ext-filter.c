/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

/* Extension vnd.dovecot.filter
 * -----------------------------
 *
 * Authors: Stephan Bosch
 * Specification: spec-bosch-sieve-extprograms
 * Implementation: full
 * Status: experimental
 *
 */
 
#include "lib.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-binary.h" 

#include "sieve-validator.h"
#include "sieve-interpreter.h"

#include "sieve-ext-copy.h"

#include "sieve-extprograms-common.h"

/* 
 * Extension 
 */

static bool ext_filter_load(const struct sieve_extension *ext, void **context);
static void ext_filter_unload(const struct sieve_extension *ext);
static bool ext_filter_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);
	
const struct sieve_extension_def filter_extension = { 
	.name = "vnd.dovecot.filter",
	.load = ext_filter_load,
	.unload = ext_filter_unload,
	.validator_load = ext_filter_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(cmd_filter_operation),
};

/*
 * Context
 */

static bool ext_filter_load(const struct sieve_extension *ext, void **context)
{
	if ( *context != NULL ) {
		ext_filter_unload(ext);
		*context = NULL;
	}

	*context = (void *)sieve_extprograms_config_init(ext);
	return TRUE;
}

static void ext_filter_unload(const struct sieve_extension *ext)
{
	struct sieve_extprograms_config *ext_config = 
		(struct sieve_extprograms_config *)ext->context;

	if ( ext_config == NULL ) return;

	sieve_extprograms_config_deinit(&ext_config);
}

/*
 * Validation
 */

static bool ext_filter_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register commands */
	sieve_validator_register_command(valdtr, ext, &cmd_filter);

	return TRUE;
}
