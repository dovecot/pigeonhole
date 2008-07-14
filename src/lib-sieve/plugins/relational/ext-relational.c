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
 * Forward declarations 
 */

static bool ext_relational_load(int ext_id);
static bool ext_relational_validator_load(struct sieve_validator *validator);
static bool ext_relational_binary_load(struct sieve_binary *sbin);

/* Extension definitions */

int ext_relational_my_id;

const struct sieve_match_type *rel_match_types[] = {
    &rel_match_value_gt, &rel_match_value_ge, &rel_match_value_lt,
    &rel_match_value_le, &rel_match_value_eq, &rel_match_value_ne,
    &rel_match_count_gt, &rel_match_count_ge, &rel_match_count_lt,
    &rel_match_count_le, &rel_match_count_eq, &rel_match_count_ne
};

const struct sieve_extension relational_extension = { 
	"relational", 
	ext_relational_load,
	ext_relational_validator_load,
	NULL, NULL, NULL,
	ext_relational_binary_load,  
	NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_relational_load(int ext_id)
{
	ext_relational_my_id = ext_id;

	return TRUE;
}

const struct sieve_match_type_extension relational_match_extension = { 
	&relational_extension,
	SIEVE_EXT_DEFINE_MATCH_TYPES(rel_match_types) 
};

/* 
 * Load extension into validator 
 */

static bool ext_relational_validator_load(struct sieve_validator *validator)
{
	sieve_match_type_register
		(validator, &value_match_type, ext_relational_my_id); 
	sieve_match_type_register
		(validator, &count_match_type, ext_relational_my_id); 

	return TRUE;
}

/* 
 * Load extension into binary 
 */

static bool ext_relational_binary_load(struct sieve_binary *sbin)
{
	sieve_match_type_extension_set
		(sbin, ext_relational_my_id, &relational_match_extension);

	return TRUE;
}


