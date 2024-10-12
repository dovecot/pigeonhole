/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension extracttext
 * ---------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5703, Section 7
 * Implementation: full
 * Status: experimental
 *
 */

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "sieve-ext-variables.h"

#include "ext-mime-common.h"

/*
 * Extension
 */

static bool
ext_extracttext_load(const struct sieve_extension *ext, void **context);
static void
ext_extracttext_unload(const struct sieve_extension *ext);
static bool
ext_extracttext_validator_load(const struct sieve_extension *ext,
			       struct sieve_validator *valdtr);

const struct sieve_extension_def extracttext_extension = {
	.name = "extracttext",
	.load = ext_extracttext_load,
	.unload = ext_extracttext_unload,
	.validator_load = ext_extracttext_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(extracttext_operation),
};

static bool
ext_extracttext_load(const struct sieve_extension *ext, void **context)
{
	struct sieve_instance *svinst = ext->svinst;
	const struct sieve_extension *var_ext;
	const struct sieve_extension *fep_ext;
	struct ext_extracttext_context *extctx;

	if (*context != NULL) {
		ext_extracttext_unload(ext);
		*context = NULL;
	}

	if (sieve_ext_variables_get_extension(ext->svinst, &var_ext) < 0)
		return FALSE;
	if (sieve_extension_register(svinst, &foreverypart_extension, FALSE,
				     &fep_ext) < 0)
		return FALSE;

	extctx = i_new(struct ext_extracttext_context, 1);
	extctx->var_ext = var_ext;
	extctx->fep_ext = fep_ext;

	*context = extctx;
	return TRUE;
}

static void ext_extracttext_unload(const struct sieve_extension *ext)
{
	struct ext_extracttext_context *extctx = ext->context;

	i_free(extctx);
}

/*
 * Extension validation
 */

static bool
ext_extracttext_validator_validate(
	const struct sieve_extension *ext, struct sieve_validator *valdtr,
	void *context, struct sieve_ast_argument *require_arg, bool required);

const struct sieve_validator_extension
extracttext_validator_extension = {
	.ext = &extracttext_extension,
	.validate = ext_extracttext_validator_validate,
};

static bool
ext_extracttext_validator_load(const struct sieve_extension *ext,
			       struct sieve_validator *valdtr)
{
	/* Register validator extension to check for conflict with eextracttext.
	 */
	sieve_validator_extension_register(
		valdtr, ext, &extracttext_validator_extension, NULL);

	/* Register new commands */
	sieve_validator_register_command(valdtr, ext, &cmd_extracttext);

	return TRUE;
}

static bool
ext_extracttext_validator_validate(const struct sieve_extension *ext,
				   struct sieve_validator *valdtr,
				   void *context ATTR_UNUSED,
				   struct sieve_ast_argument *require_arg,
				   bool required ATTR_UNUSED)
{
	struct ext_extracttext_context *extctx = ext->context;

	if (extctx->var_ext == NULL ||
	    !sieve_ext_variables_is_active(extctx->var_ext, valdtr) ) {
		sieve_argument_validate_error(
			valdtr, require_arg,
			"extracttext extension cannot be used "
			"without variables extension");
		return FALSE;
	}
	if (extctx->fep_ext == NULL ||
	    !sieve_validator_extension_loaded(valdtr, extctx->fep_ext) ) {
		sieve_argument_validate_error(
			valdtr, require_arg,
			"extracttext extension cannot be used "
			"without foreverypart extension");
		return FALSE;
	}
	return TRUE;
}
