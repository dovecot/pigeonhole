/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

/* Extension duplicate
 * -------------------
 *
 * Authors: Stephan Bosch
 * Specification: vendor-defined; spec-bosch-sieve-duplicate
 * Implementation: full
 * Status: experimental
 *
 */

/* Extension vnd.dovecot.duplicate
 * -------------------------------
 *
 * Authors: Stephan Bosch
 * Specification: vendor-defined; spec-bosch-sieve-duplicate
 * Implementation: full, but deprecated; provided for backwards compatibility
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
 * Extensions
 */

static bool ext_duplicate_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);

const struct sieve_extension_def duplicate_extension = {
	.name = "duplicate",
	.load = ext_duplicate_load,
	.unload = ext_duplicate_unload,
	.validator_load = ext_duplicate_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(tst_duplicate_operation)
};

const struct sieve_extension_def vnd_duplicate_extension = {
	.name = "vnd.dovecot.duplicate",
	.load = ext_duplicate_load,
	.unload = ext_duplicate_unload,
	.validator_load = ext_duplicate_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(tst_duplicate_operation)
};

/*
 * Validation
 */

static bool ext_duplicate_validator_check_conflict
	(const struct sieve_extension *ext,
		struct sieve_validator *valdtr, void *context,
		struct sieve_ast_argument *require_arg,
		const struct sieve_extension *ext_other,
		bool required);

const struct sieve_validator_extension
duplicate_validator_extension = {
	.ext = &vnd_duplicate_extension,
	.check_conflict = ext_duplicate_validator_check_conflict
};

static bool ext_duplicate_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register validator extension to check for conflict between
	   vnd.dovecot.duplicate and duplicate extensions */
	if ( sieve_extension_is(ext, vnd_duplicate_extension) ) {
		sieve_validator_extension_register
			(valdtr, ext, &duplicate_validator_extension, NULL);
	}

	/* Register duplicate test */
	sieve_validator_register_command(valdtr, ext, &tst_duplicate);

	return TRUE;
}

static bool ext_duplicate_validator_check_conflict
(const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_validator *valdtr, void *context ATTR_UNUSED,
	struct sieve_ast_argument *require_arg,
	const struct sieve_extension *ext_other,
	bool required ATTR_UNUSED)
{
	/* Check for conflict with duplicate extension */
	if ( sieve_extension_name_is(ext_other, "duplicate") ) {
		sieve_argument_validate_error(valdtr, require_arg,
			"the (deprecated) vnd.dovecot.duplicate extension "
			"cannot be used together with the duplicate extension");
		return FALSE;
	}

	return TRUE;
}

