/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension relational 
 * --------------------
 *
 * Author: Stephan Bosch
 * Specification: RFC 3431
 * Implementation: full
 * Status: experimental, largely untested
 * 
 */

#include "lib.h"
#include "str.h"

#include "sieve-common.h"

#include "sieve-ast.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-relational-common.h"

/* 
 * Extension
 */

static bool ext_relational_load(int ext_id);
static bool ext_relational_validator_load(struct sieve_validator *validator);

int ext_relational_my_id;

const struct sieve_extension relational_extension = { 
	"relational", 
	&ext_relational_my_id,
	ext_relational_load,
	ext_relational_validator_load,
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_OPERAND(rel_match_type_operand)
};

static bool ext_relational_load(int ext_id)
{
	ext_relational_my_id = ext_id;

	return TRUE;
}

static bool ext_relational_validator_load(struct sieve_validator *validator)
{
	sieve_match_type_register(validator, &value_match_type); 
	sieve_match_type_register(validator, &count_match_type); 

	return TRUE;
}


