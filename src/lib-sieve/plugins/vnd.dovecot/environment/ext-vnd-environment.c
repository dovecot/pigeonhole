/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

/* Extension vnd.dovecot.environment
 * ---------------------------------
 *
 * Authors: Stephan Bosch
 * Specification: vendor-defined;
 *   spec-bosch-sieve-dovecot-environment
 * Implementation: preliminary
 * Status: experimental
 *
 */

#include "lib.h"
#include "array.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "sieve-ext-variables.h"

#include "ext-vnd-environment-common.h"

/*
 * Extension
 */

static bool ext_vnd_environment_load
	(const struct sieve_extension *ext, void **context);
static void ext_vnd_environment_unload
	(const struct sieve_extension *ext);
static bool ext_vnd_environment_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);
static bool ext_vnd_environment_interpreter_load
	(const struct sieve_extension *ext, const struct sieve_runtime_env *renv,
		sieve_size_t *address);

const struct sieve_extension_def vnd_environment_extension = {
	.name = "vnd.dovecot.environment",
	.load = ext_vnd_environment_load,
	.unload = ext_vnd_environment_unload,
	.validator_load = ext_vnd_environment_validator_load,
	.interpreter_load = ext_vnd_environment_interpreter_load,
	SIEVE_EXT_DEFINE_OPERAND(environment_namespace_operand)
};

static bool ext_vnd_environment_load
(const struct sieve_extension *ext, void **context)
{
	struct ext_vnd_environment_context *ectx;

	if ( *context != NULL )
		ext_vnd_environment_unload(ext);

	ectx = i_new(struct ext_vnd_environment_context, 1);
	ectx->env_ext = sieve_ext_environment_require_extension(ext->svinst);
	ectx->var_ext = sieve_ext_variables_get_extension(ext->svinst);
	*context = (void *) ectx;

	return TRUE;
}

static void ext_vnd_environment_unload
(const struct sieve_extension *ext)
{
	struct ext_vnd_environment_context *ectx =
		(struct ext_vnd_environment_context *) ext->context;

	i_free(ectx);
}

/*
 * Validator
 */

static bool ext_vnd_environment_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	const struct sieve_extension *env_ext;

	/* Load environment extension implicitly */

	env_ext = sieve_validator_extension_load_implicit
		(valdtr, environment_extension.name);
	if ( env_ext == NULL )
		return FALSE;

	ext_environment_variables_init(ext, valdtr);
	return TRUE;
}

/*
 * Interpreter
 */

static bool ext_vnd_environment_interpreter_load
(const struct sieve_extension *ext, const struct sieve_runtime_env *renv,
	sieve_size_t *address ATTR_UNUSED)
{
	ext_vnd_environment_items_register(ext, renv);
	return TRUE;
}
