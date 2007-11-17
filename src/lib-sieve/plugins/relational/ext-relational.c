/* Extension relational 
 * --------------------
 *
 * Author: Stephan Bosch
 * Specification: RFC 3431
 * Implementation: 
 * Status: under development
 * 
 */

/* Syntax:
 *   MATCH-TYPE =/ COUNT / VALUE
 *   COUNT = ":count" relational-match
 *   VALUE = ":value" relational-match
 *   relational-match = DQUOTE ( "gt" / "ge" / "lt"
 *                             / "le" / "eq" / "ne" ) DQUOTE
 */ 

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Forward declarations */

static bool ext_relational_load(int ext_id);
static bool ext_relational_validator_load(struct sieve_validator *validator);
static bool ext_relational_interpreter_load
	(struct sieve_interpreter *interpreter);

/* Extension definitions */

static int ext_my_id;

const struct sieve_extension relational_extension = { 
	"relational", 
	ext_relational_load,
	ext_relational_validator_load,
	NULL, 
	ext_relational_interpreter_load,  
	NULL, 
	NULL
};

static bool ext_relational_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Actual extension implementation */


/* Extension access structures */

enum ext_relational_match_type {
  RELATIONAL_VALUE,
  RELATIONAL_COUNT
};

extern const struct sieve_match_type_extension relational_match_extension;

const struct sieve_match_type value_match_type = {
	"value",
	SIEVE_MATCH_TYPE_CUSTOM,
	&relational_match_extension,
	RELATIONAL_VALUE
};

const struct sieve_match_type count_match_type = {
	"count",
	SIEVE_MATCH_TYPE_CUSTOM,
	&relational_match_extension,
	RELATIONAL_COUNT
};

static const struct sieve_match_type *ext_relational_get_match 
	(unsigned int code)
{
	switch ( code ) {
	case RELATIONAL_VALUE:
		return &value_match_type;
	case RELATIONAL_COUNT:
		return &count_match_type;
	default:
		break;
	}
	
	return NULL;
}

const struct sieve_match_type_extension relational_match_extension = { 
	&relational_extension,
	NULL, 
	ext_relational_get_match
};

/* Load extension into validator */

static bool ext_relational_validator_load(struct sieve_validator *validator)
{
	sieve_match_type_register
		(validator, &value_match_type, ext_my_id); 
	sieve_match_type_register
		(validator, &count_match_type, ext_my_id); 

	return TRUE;
}

/* Load extension into interpreter */

static bool ext_relational_interpreter_load
	(struct sieve_interpreter *interpreter)
{
	sieve_match_type_extension_set
		(interpreter, ext_my_id, &relational_match_extension);

	return TRUE;
}


