/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

/* Extension vnd.dovecot.duplicate
 * -------------------------------
 *
 * Authors: Stephan Bosch
 * Specification: spec-bosch-sieve-duplicate
 * Implementation: full
 * Status: experimental
 *
 */

#include "lib.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-binary.h"

#include "sieve-validator.h"

#include "ext-duplicate-common.h"

/*
 * Extension
 */

static bool ext_duplicate_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);

const struct sieve_extension_def duplicate_extension = {
	.name = "vnd.dovecot.duplicate",
	.load = ext_duplicate_load,
	.unload = ext_duplicate_unload,
	.validator_load = ext_duplicate_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(tst_duplicate_operation)
};

/*
 * Validation
 */

static bool ext_duplicate_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register duplicate test */
	sieve_validator_register_command(valdtr, ext, &tst_duplicate);

	return TRUE;
}
