/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension enotify
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5435
 * Implementation: full
 * Status: testing
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

#include "ext-enotify-common.h"

/*
 * Operations
 */

const struct sieve_operation_def *ext_enotify_operations[] = {
	&notify_operation,
	&valid_notify_method_operation,
	&notify_method_capability_operation,
};

/*
 * Extension
 */

static int
ext_enotify_load(const struct sieve_extension *ext, void **context_r);
static void ext_enotify_unload(const struct sieve_extension *ext);
static bool
ext_enotify_validator_load(const struct sieve_extension *ext,
			   struct sieve_validator *valdtr);

const struct sieve_extension_def enotify_extension = {
	.name = "enotify",
	.load = ext_enotify_load,
	.unload = ext_enotify_unload,
	.validator_load = ext_enotify_validator_load,
	SIEVE_EXT_DEFINE_OPERATIONS(ext_enotify_operations),
	SIEVE_EXT_DEFINE_OPERAND(encodeurl_operand),
};

static int ext_enotify_load(const struct sieve_extension *ext, void **context_r)
{
	const struct sieve_extension *var_ext;
	struct ext_enotify_context *extctx;

	if (sieve_ext_variables_get_extension(ext->svinst, &var_ext) < 0)
		return -1;

	extctx = i_new(struct ext_enotify_context, 1);
	extctx->var_ext = var_ext;

	if (ext_enotify_methods_init(extctx, ext) < 0) {
		i_free(extctx);
		return -1;
	}

	sieve_extension_capabilities_register(ext, &notify_capabilities);

	*context_r = extctx;
	return 0;
}

static void ext_enotify_unload(const struct sieve_extension *ext)
{
	struct ext_enotify_context *extctx = ext->context;

	if (extctx == NULL)
		return;

	ext_enotify_methods_deinit(extctx);
	i_free(extctx);
}

static bool
ext_enotify_validator_load(const struct sieve_extension *ext,
			   struct sieve_validator *valdtr)
{
	struct ext_enotify_context *extctx = ext->context;

	/* Register new commands */
	sieve_validator_register_command(valdtr, ext,
					 &notify_command);
	sieve_validator_register_command(valdtr, ext,
					 &valid_notify_method_test);
	sieve_validator_register_command(valdtr, ext,
					 &notify_method_capability_test);

	/* Register new set modifier for variables extension */
	sieve_variables_modifier_register(extctx->var_ext, valdtr, ext,
					  &encodeurl_modifier);

	return TRUE;
}
