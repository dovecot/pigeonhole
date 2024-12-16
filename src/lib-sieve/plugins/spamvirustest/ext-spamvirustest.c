/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extensions spamtest, spamtestplus and virustest
 * -----------------------------------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5235
 * Implementation: full
 * Status: testing
 *
 */

#include "lib.h"
#include "array.h"

#include "sieve-common.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"

#include "sieve-validator.h"

#include "ext-spamvirustest-common.h"

/*
 * Extensions
 */

/* Spamtest */

static bool ext_spamvirustest_validator_load
(const struct sieve_extension *ext, struct sieve_validator *validator);

const struct sieve_extension_def spamtest_extension = {
	.name = "spamtest",
	.load = ext_spamvirustest_load,
	.unload = ext_spamvirustest_unload,
	.validator_load = ext_spamvirustest_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(spamtest_operation)
};

const struct sieve_extension_def spamtestplus_extension = {
	.name = "spamtestplus",
	.load = ext_spamvirustest_load,
	.unload = ext_spamvirustest_unload,
	.validator_load = ext_spamvirustest_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(spamtest_operation)
};

const struct sieve_extension_def virustest_extension = {
	.name = "virustest",
	.load = ext_spamvirustest_load,
	.unload = ext_spamvirustest_unload,
	.validator_load = ext_spamvirustest_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(virustest_operation)
};

/*
 * Implementation
 */

static bool ext_spamtest_validator_check_conflict
	(const struct sieve_extension *ext,
		struct sieve_validator *valdtr, void *context,
		struct sieve_ast_argument *require_arg,
		const struct sieve_extension *ext_other,
		bool required);

const struct sieve_validator_extension spamtest_validator_extension = {
	.ext = &spamtest_extension,
	.check_conflict = ext_spamtest_validator_check_conflict
};

static bool ext_spamvirustest_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register new test */

	if ( sieve_extension_is(ext, virustest_extension) ) {
		sieve_validator_register_command(valdtr, ext, &virustest_test);
	} else {
		if ( sieve_extension_is(ext, spamtest_extension) ) {
			/* Register validator extension to warn for duplicate */
			sieve_validator_extension_register
				(valdtr, ext, &spamtest_validator_extension, NULL);
		}

		sieve_validator_register_command(valdtr, ext, &spamtest_test);
	}

	return TRUE;
}

static bool ext_spamtest_validator_check_conflict
(const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_validator *valdtr, void *context ATTR_UNUSED,
	struct sieve_ast_argument *require_arg,
	const struct sieve_extension *ext_other,
	bool required ATTR_UNUSED)
{
	if ( sieve_extension_name_is(ext_other, "spamtestplus") ) {
		sieve_argument_validate_warning(valdtr, require_arg,
			"the spamtest and spamtestplus extensions should "
			"not be specified at the same time");
	}

	return TRUE;
}


