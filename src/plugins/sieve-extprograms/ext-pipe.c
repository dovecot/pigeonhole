/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

/* Extension vnd.dovecot.pipe
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

static bool ext_pipe_load(const struct sieve_extension *ext, void **context);
static void ext_pipe_unload(const struct sieve_extension *ext);
static bool ext_pipe_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);
	
const struct sieve_extension_def pipe_extension = { 
	.name = "vnd.dovecot.pipe",
	.load = ext_pipe_load,
	.unload = ext_pipe_unload,
	.validator_load = ext_pipe_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(cmd_pipe_operation),
};

/*
 * Context
 */

static bool ext_pipe_load(const struct sieve_extension *ext, void **context)
{
	if ( *context != NULL ) {
		ext_pipe_unload(ext);
		*context = NULL;
	}

	*context = (void *)sieve_extprograms_config_init(ext);
	return TRUE;
}

static void ext_pipe_unload(const struct sieve_extension *ext)
{
	struct sieve_extprograms_config *ext_config = 
		(struct sieve_extprograms_config *)ext->context;

	if ( ext_config == NULL ) return;

	sieve_extprograms_config_deinit(&ext_config);
}

/*
 * Validation
 */

static bool ext_pipe_validator_extension_validate
	(const struct sieve_extension *ext, struct sieve_validator *valdtr, 
		void *context, struct sieve_ast_argument *require_arg);

const struct sieve_validator_extension pipe_validator_extension = {
	&pipe_extension,
	ext_pipe_validator_extension_validate,
	NULL
};

static bool ext_pipe_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register commands */
	sieve_validator_register_command(valdtr, ext, &cmd_pipe);

	/* Register extension to validator */
	sieve_validator_extension_register
		(valdtr, ext, &pipe_validator_extension, NULL);

	return TRUE;
}

static bool ext_pipe_validator_extension_validate
(const struct sieve_extension *ext, struct sieve_validator *valdtr,
	void *context ATTR_UNUSED, struct sieve_ast_argument *require_arg ATTR_UNUSED)
{
	struct sieve_extprograms_config *ext_config =
		(struct sieve_extprograms_config *) ext->context;

	if ( ext_config != NULL && ext_config->copy_ext != NULL ) {
		/* Register :copy command tag */
		sieve_ext_copy_register_tag
			(valdtr, ext_config->copy_ext, cmd_pipe.identifier);
	}
	return TRUE;
}


