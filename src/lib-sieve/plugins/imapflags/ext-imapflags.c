/* Extension imapflags
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-imapflags-05
 * Implementation: skeleton
 * Status: under development
 *
 */

#include <stdio.h>

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Forward declarations */

static bool ext_imapflags_load(int ext_id);
static bool ext_imapflags_validator_load(struct sieve_validator *validator);

/* Extension definitions */

int ext_my_id;

const struct sieve_extension imapflags_extension = { 
	"imapflags", 
	ext_imapflags_load,
	ext_imapflags_validator_load, 
	NULL, 
	NULL, 
	NULL, 
	NULL
};

static bool ext_imapflags_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Load extension into validator */

static bool ext_imapflags_validator_load
	(struct sieve_validator *validator ATTR_UNUSED)
{
	/* Register new command */
	//sieve_validator_register_command(validator, &imapflags_command);

	return TRUE;
}


