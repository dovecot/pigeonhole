/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

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
#include <regex.h>

/* 
 * Extension
 */

static bool ext_regex_load(int ext_id);
static bool ext_regex_validator_load(struct sieve_validator *validator);

static int ext_my_id;

const struct sieve_extension regex_extension = { 
	"regex", 
	&ext_my_id,
	ext_regex_load,
	NULL,
	ext_regex_validator_load,
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_OPERAND(regex_match_type_operand)
};

static bool ext_regex_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

static bool ext_regex_validator_load(struct sieve_validator *validator)
{
	sieve_match_type_register(validator, &regex_match_type); 

	return TRUE;
}


