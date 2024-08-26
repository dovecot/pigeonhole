/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
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
#include "settings.h"

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

static int
ext_vnd_environment_load(const struct sieve_extension *ext, void **context_r);
static void
ext_vnd_environment_unload(const struct sieve_extension *ext);
static bool
ext_vnd_environment_validator_load(const struct sieve_extension *ext,
				   struct sieve_validator *valdtr);
static bool
ext_vnd_environment_interpreter_load(const struct sieve_extension *ext,
				     const struct sieve_runtime_env *renv,
				     sieve_size_t *address);

const struct sieve_extension_def vnd_environment_extension = {
	.name = "vnd.dovecot.environment",
	.load = ext_vnd_environment_load,
	.unload = ext_vnd_environment_unload,
	.validator_load = ext_vnd_environment_validator_load,
	.interpreter_load = ext_vnd_environment_interpreter_load,
	SIEVE_EXT_DEFINE_OPERAND(environment_namespace_operand),
};

static int
ext_vnd_environment_load(const struct sieve_extension *ext, void **context_r)
{
	const struct sieve_extension *ext_env;
	const struct sieve_extension *ext_var;
	struct sieve_instance *svinst = ext->svinst;
	struct ext_vnd_environment_context *extctx;
	const struct ext_vnd_environment_settings *set;
	const char *error;

	if (sieve_ext_environment_require_extension(ext->svinst, &ext_env) < 0)
		return -1;
	if (sieve_ext_variables_get_extension(ext->svinst, &ext_var) < 0)
		return -1;

	if (settings_get(svinst->event,
			 &ext_vnd_environment_setting_parser_info, 0,
			 &set, &error) < 0) {
		e_error(svinst->event, "%s", error);
		return -1;
	}

	extctx = i_new(struct ext_vnd_environment_context, 1);
	extctx->set = set;
	extctx->env_ext = ext_env;
	extctx->var_ext = ext_var;

	*context_r = extctx;
	return 0;
}

static void ext_vnd_environment_unload(const struct sieve_extension *ext)
{
	struct ext_vnd_environment_context *extctx = ext->context;

	if (extctx == NULL)
		return;
	settings_free(extctx->set);
	i_free(extctx);
}

/*
 * Validator
 */

static bool
ext_vnd_environment_validator_load(const struct sieve_extension *ext,
				   struct sieve_validator *valdtr)
{
	const struct sieve_extension *env_ext;

	/* Load environment extension implicitly */

	env_ext = sieve_validator_extension_load_implicit(
		valdtr, environment_extension.name);
	if (env_ext == NULL)
		return FALSE;

	ext_environment_variables_init(ext, valdtr);
	return TRUE;
}

/*
 * Interpreter
 */

static bool
ext_vnd_environment_interpreter_load(const struct sieve_extension *ext,
				     const struct sieve_runtime_env *renv,
				     sieve_size_t *address ATTR_UNUSED)
{
	ext_vnd_environment_items_register(ext, renv);
	return TRUE;
}
