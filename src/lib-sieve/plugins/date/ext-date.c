/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension date
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5260
 * Implementation: full
 * Status: experimental
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

static bool ext_date_validator_load(struct sieve_validator *validator);

int ext_date_my_id = -1;

const struct sieve_operation *ext_date_operations[] = {
	&date_operation,
	&currentdate_operation
};

const struct sieve_extension date_extension = { 
	"date", 
	&ext_date_my_id,
	NULL, NULL,
	ext_date_validator_load, 
	NULL, 
	ext_date_interpreter_load, 
	NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_OPERATIONS(ext_date_operations), 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_date_validator_load(struct sieve_validator *valdtr)
{
	/* Register new test */
	sieve_validator_register_command(valdtr, &date_test);
	sieve_validator_register_command(valdtr, &currentdate_test);

	return TRUE;
}


