/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension imap4flags
 * --------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5232
 * Implementation: full
 * Status: testing
 *
 */

#include "lib.h"
#include "mempool.h"
#include "str.h"

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imap4flags-common.h"

/*
 * Operations
 */

const struct sieve_operation_def *imap4flags_operations[] = {
	&setflag_operation,
	&addflag_operation,
	&removeflag_operation,
	&hasflag_operation
};

/*
 * Extension
 */

static int
ext_imap4flags_load(const struct sieve_extension *ext, void **context_r);
static void ext_imap4flags_unload(const struct sieve_extension *ext);

static bool ext_imap4flags_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);
static bool ext_imap4flags_interpreter_load
	(const struct sieve_extension *ext, const struct sieve_runtime_env *renv,
		sieve_size_t *address);

const struct sieve_extension_def imap4flags_extension = {
	.name = "imap4flags",
	.version = 1,
	.load = ext_imap4flags_load,
	.unload = ext_imap4flags_unload,
	.validator_load = ext_imap4flags_validator_load,
	.interpreter_load = ext_imap4flags_interpreter_load,
	SIEVE_EXT_DEFINE_OPERATIONS(imap4flags_operations),
	SIEVE_EXT_DEFINE_OPERAND(flags_side_effect_operand)
};

static int
ext_imap4flags_load(const struct sieve_extension *ext, void **context_r)
{
	struct sieve_instance *svinst = ext->svinst;
	const struct sieve_extension *var_ext;
	struct ext_imap4flags_context *extctx;

	if (sieve_ext_variables_get_extension(svinst, &var_ext) < 0)
		return -1;

	extctx = i_new(struct ext_imap4flags_context, 1);
	extctx->var_ext = var_ext;

	*context_r = extctx;
	return 0;
}

static void ext_imap4flags_unload(const struct sieve_extension *ext)
{
	struct ext_imap4flags_context *extctx = ext->context;

	i_free(extctx);
}

static bool ext_imap4flags_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register commands */
	sieve_validator_register_command(valdtr, ext, &cmd_setflag);
	sieve_validator_register_command(valdtr, ext, &cmd_addflag);
	sieve_validator_register_command(valdtr, ext, &cmd_removeflag);
	sieve_validator_register_command(valdtr, ext, &tst_hasflag);

	/* Attach :flags tag to keep and fileinto commands */
	ext_imap4flags_attach_flags_tag(valdtr, ext, "keep");
	ext_imap4flags_attach_flags_tag(valdtr, ext, "fileinto");

	/* Attach flags side-effect to keep and fileinto actions */
	sieve_ext_imap4flags_register_side_effect(valdtr, ext, "keep");
	sieve_ext_imap4flags_register_side_effect(valdtr, ext, "fileinto");

	return TRUE;
}

void sieve_ext_imap4flags_interpreter_load
(const struct sieve_extension *ext, const struct sieve_runtime_env *renv)
{
	sieve_interpreter_extension_register
		(renv->interp, ext, &imap4flags_interpreter_extension, NULL);
}

static bool ext_imap4flags_interpreter_load
(const struct sieve_extension *ext, const struct sieve_runtime_env *renv,
	sieve_size_t *address ATTR_UNUSED)
{
	sieve_ext_imap4flags_interpreter_load(ext, renv);
	return TRUE;
}



