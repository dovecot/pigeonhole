/* Extension body 
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-body-07
 * Implementation: full, but needs some more work
 * Status: experimental, largely untested
 *
 */
 
#include "lib.h"
#include "array.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

#include "ext-body-common.h"

/* 
 * Commands
 */

extern const struct sieve_command body_test;
 
/*
 * Opcodes
 */

extern const struct sieve_opcode body_opcode;

/* 
 * Extension definitions 
 */

int ext_body_my_id;

static bool ext_body_load(int ext_id);
static bool ext_body_validator_load(struct sieve_validator *validator);

const struct sieve_extension body_extension = { 
	"body", 
	ext_body_load,
	ext_body_validator_load, 
	NULL, NULL, NULL, 
	SIEVE_EXT_DEFINE_OPCODE(body_opcode), 
	NULL 
};

static bool ext_body_load(int ext_id) 
{
	ext_body_my_id = ext_id;
	return TRUE;
}

/* Load extension into validator */

static bool ext_body_validator_load(struct sieve_validator *validator)
{
	/* Register new test */
	sieve_validator_register_command(validator, &body_test);

	return TRUE;
}


