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

bool ext_vacation_seconds_load(const struct sieve_extension *ext,
			       void **context);
static bool
ext_vacation_seconds_validator_load(const struct sieve_extension *ext,
				    struct sieve_validator *valdtr);

const struct sieve_extension_def vacation_seconds_extension = {
	.name = "vacation-seconds",
	.load = ext_vacation_seconds_load,
	.validator_load = ext_vacation_seconds_validator_load,
};

bool ext_vacation_seconds_load(const struct sieve_extension *ext,
			       void **context)
{
	if (*context == NULL) {
		/* Make sure vacation extension is registered */
		*context = (void *)
			sieve_extension_require(ext->svinst,
						&vacation_extension, TRUE);
	}
	return TRUE;
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
