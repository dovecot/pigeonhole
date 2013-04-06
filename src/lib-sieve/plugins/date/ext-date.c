/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

/* Extension date
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5260
 * Implementation: full
 * Status: testing
 *
 */

#include "lib.h"
#include "array.h"

#include "sieve-common.h"

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

#include "ext-date-common.h"

/*
 * Extension
 */

static bool ext_date_validator_load
(const struct sieve_extension *ext, struct sieve_validator *validator);

const struct sieve_operation_def *ext_date_operations[] = {
	&date_operation,
	&currentdate_operation
};

const struct sieve_extension_def date_extension = {
	.name = "date",
	.validator_load = ext_date_validator_load,
	.interpreter_load = ext_date_interpreter_load,
	SIEVE_EXT_DEFINE_OPERATIONS(ext_date_operations)
};

static bool ext_date_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register new test */
	sieve_validator_register_command(valdtr, ext, &date_test);
	sieve_validator_register_command(valdtr, ext, &currentdate_test);

	return TRUE;
}


