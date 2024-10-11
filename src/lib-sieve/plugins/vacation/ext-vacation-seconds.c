/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension vacation-seconds
 * --------------------------
 *
 * Authors: Stephan Bosch <stephan@rename-it.nl>
 * Specification: RFC 6131
 * Implementation: full
 * Status: testing
 *
 */

#include "lib.h"

#include "sieve-common.h"

#include "sieve-extensions.h"
#include "sieve-validator.h"

#include "ext-vacation-common.h"

/*
 * Extension
 */

struct ext_vacation_seconds_context {
	const struct sieve_extension *ext_vacation;
};

static bool
ext_vacation_seconds_load(const struct sieve_extension *ext, void **context);
static void ext_vacation_seconds_unload(const struct sieve_extension *ext);

static bool
ext_vacation_seconds_validator_load(const struct sieve_extension *ext,
				    struct sieve_validator *valdtr);

const struct sieve_extension_def vacation_seconds_extension = {
	.name = "vacation-seconds",
	.load = ext_vacation_seconds_load,
	.unload = ext_vacation_seconds_unload,
	.validator_load = ext_vacation_seconds_validator_load,
};

static bool
ext_vacation_seconds_load(const struct sieve_extension *ext, void **context)
{
	const struct sieve_extension *ext_vac;
	struct ext_vacation_seconds_context *extctx;

	if (*context != NULL) {
		ext_vacation_seconds_unload(ext);
		*context = NULL;
	}

	/* Make sure vacation extension is registered */
	if (sieve_extension_require(ext->svinst, &vacation_extension,
				    TRUE, &ext_vac) < 0)
		return FALSE;

	extctx = i_new(struct ext_vacation_seconds_context, 1);
	extctx->ext_vacation = ext_vac;

	*context = extctx;
	return TRUE;
}

static void ext_vacation_seconds_unload(const struct sieve_extension *ext)
{
	struct ext_vacation_seconds_context *extctx = ext->context;

	if (extctx == NULL)
		return;
	i_free(extctx);
}

static bool
ext_vacation_seconds_validator_load(
	const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_validator *valdtr)
{
	const struct sieve_extension *vacation_ext;

	/* Load vacation extension implicitly */

	vacation_ext = sieve_validator_extension_load_implicit(
		valdtr, vacation_extension.name);
	if (vacation_ext == NULL)
		return FALSE;

	/* Add seconds tag to vacation command */

	return ext_vacation_register_seconds_tag(valdtr, vacation_ext);
}
