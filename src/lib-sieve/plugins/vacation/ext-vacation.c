/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension vacation
 * ------------------
 *
 * Authors: Stephan Bosch <stephan@rename-it.nl>
 * Specification: RFC 5230
 * Implementation: almost complete; the required sopport for Refences header 
 *   is missing.
 * Status: experimental, largely untested
 * 
 */

#include "lib.h"

#include "sieve-common.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-vacation-common.h"

/* 
 * Extension
 */

static bool ext_vacation_load(int ext_id);
static bool ext_vacation_validator_load(struct sieve_validator *validator);

static int ext_my_id;

const struct sieve_extension vacation_extension = { 
	"vacation",
	&ext_my_id,
	ext_vacation_load,
	ext_vacation_validator_load, 
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_OPERATION(vacation_operation),
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_vacation_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

static bool ext_vacation_validator_load(struct sieve_validator *validator)
{
	/* Register new command */
	sieve_validator_register_command(validator, &vacation_command);

	return TRUE;
}
