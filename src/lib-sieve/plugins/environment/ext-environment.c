/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension variables 
 * -------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5183
 * Implementation: full
 * Status: experimental, not thoroughly tested
 *
 */
 
#include "lib.h"
#include "str.h"
#include "unichar.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"

#include "sieve-validator.h"

#include "ext-environment-common.h"

/* 
 * Extension 
 */

static bool ext_environment_validator_load(struct sieve_validator *validator);

static int ext_my_id = -1;
	
const struct sieve_extension environment_extension = { 
	"environment", 
	&ext_my_id,
	ext_environment_init, 
	ext_environment_deinit,
	ext_environment_validator_load,
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_OPERATION(tst_environment_operation), 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_environment_validator_load
	(struct sieve_validator *validator)
{
	sieve_validator_register_command(validator, &tst_environment);
	
	return TRUE;
}

