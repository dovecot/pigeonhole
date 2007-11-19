/* Extension regex 
 * --------------------
 *
 * Author: Stephan Bosch
 * Specification: draft-murchison-sieve-regex-07
 * Implementation: skeleton
 * Status: under development
 * 
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

static bool ext_regex_load(int ext_id);
static bool ext_regex_validator_load(struct sieve_validator *validator);
static bool ext_regex_interpreter_load
	(struct sieve_interpreter *interpreter);

/* Extension definitions */

static int ext_my_id;

const struct sieve_extension regex_extension = { 
	"regex", 
	ext_regex_load,
	ext_regex_validator_load,
	NULL, 
	ext_regex_interpreter_load,  
	NULL, 
	NULL
};

static bool ext_regex_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Actual extension implementation */


/* Extension access structures */

extern const struct sieve_match_type_extension regex_match_extension;

const struct sieve_match_type regex_match_type = {
	"regex",
	SIEVE_MATCH_TYPE_CUSTOM,
	&regex_match_extension,
	0,
	NULL,
	NULL
};

const struct sieve_match_type_extension regex_match_extension = { 
	&regex_extension,
	&regex_match_type, 
	NULL
};

/* Load extension into validator */

static bool ext_regex_validator_load(struct sieve_validator *validator)
{
	sieve_match_type_register
		(validator, &regex_match_type, ext_my_id); 

	return TRUE;
}

/* Load extension into interpreter */

static bool ext_regex_interpreter_load
	(struct sieve_interpreter *interpreter)
{
	sieve_match_type_extension_set
		(interpreter, ext_my_id, &regex_match_extension);

	return TRUE;
}


