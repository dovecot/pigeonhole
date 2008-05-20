/* Extension regex 
 * ---------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-murchison-sieve-regex-07
 * Implementation: full, but suboptimal
 * Status: experimental, largely untested
 *
 * FIXME: Regular expressions are compiled during compilation and 
 * again during interpretation. This is suboptimal and should be 
 * changed. This requires dumping the compiled regex to the binary. 
 * Most likely, this will only be possible when we implement regular
 * expressions ourselves. 
 * 
 */

#include "lib.h"
#include "mempool.h"
#include "buffer.h"

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"

#include "sieve-comparators.h"
#include "sieve-match-types.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-regex-common.h"

#include <sys/types.h>
#include <ctype.h>
#include <regex.h>

/* Forward declarations */

static bool ext_regex_load(int ext_id);
static bool ext_regex_validator_load(struct sieve_validator *validator);
static bool ext_regex_binary_load(struct sieve_binary *sbin);

/* Extension definitions */

static int ext_my_id;

const struct sieve_extension regex_extension = { 
	"regex", 
	ext_regex_load,
	ext_regex_validator_load,
	NULL, 
	NULL,
	ext_regex_binary_load,
	NULL,  
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_regex_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* 
 * Extension access structures 
 */

extern const struct sieve_match_type regex_match_type;

const struct sieve_match_type_extension regex_match_extension = { 
	&regex_extension,
	SIEVE_EXT_DEFINE_MATCH_TYPE(regex_match_type)
};

/* 
 * Load extension into validator 
 */

static bool ext_regex_validator_load(struct sieve_validator *validator)
{
	sieve_match_type_register
		(validator, &regex_match_type, ext_my_id); 

	return TRUE;
}

/* 
 * Load extension into binary 
 */

static bool ext_regex_binary_load(struct sieve_binary *sbin)
{
	sieve_match_type_extension_set
		(sbin, ext_my_id, &regex_match_extension);

	return TRUE;
}


