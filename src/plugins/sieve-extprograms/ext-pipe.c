/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension vnd.dovecot.pipe
 * -----------------------------
 *
 * Authors: Stephan Bosch
 * Specification: vendor-define; spec-bosch-sieve-extprograms
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

static bool
ext_pipe_validator_load(const struct sieve_extension *ext,
			struct sieve_validator *valdtr);

const struct sieve_extension_def sieve_ext_vnd_pipe = {
	.name = "vnd.dovecot.pipe",
	.load = sieve_extprograms_ext_load,
	.unload = sieve_extprograms_ext_unload,
	.validator_load = ext_pipe_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(sieve_opr_pipe),
};

/*
 * Validation
 */

static bool
ext_pipe_validator_validate(const struct sieve_extension *ext,
			    struct sieve_validator *valdtr, void *context,
			    struct sieve_ast_argument *require_arg,
			    bool required);

static const struct sieve_validator_extension pipe_validator_extension = {
	.ext = &sieve_ext_vnd_pipe,
	.validate = ext_pipe_validator_validate,
};

static bool
ext_pipe_validator_load(const struct sieve_extension *ext,
			struct sieve_validator *valdtr)
{
	/* Register commands */
	sieve_validator_register_command(valdtr, ext, &sieve_cmd_pipe);

	/* Register extension to validator */
	sieve_validator_extension_register(valdtr, ext,
					   &pipe_validator_extension, NULL);

	return TRUE;
}

static bool
ext_pipe_validator_validate(const struct sieve_extension *ext,
			    struct sieve_validator *valdtr,
			    void *context ATTR_UNUSED,
			    struct sieve_ast_argument *require_arg ATTR_UNUSED,
			    bool required ATTR_UNUSED)
{
	struct sieve_extprograms_ext_context *extctx = ext->context;

	if (extctx != NULL && extctx->copy_ext != NULL) {
		/* Register :copy command tag */
		sieve_ext_copy_register_tag(valdtr, extctx->copy_ext,
					    sieve_cmd_pipe.identifier);
	}
	return TRUE;
}
