/* Extension copy
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 3894
 * Implementation: 
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

static bool ext_copy_load(int ext_id);
static bool ext_copy_validator_load(struct sieve_validator *validator);

/* Extension definitions */

int ext_my_id;

const struct sieve_extension copy_extension = { 
	"copy", 
	ext_copy_load,
	ext_copy_validator_load, 
	NULL, NULL, NULL, 
	SIEVE_EXT_DEFINE_NO_OPCODES,
	NULL
};

static bool ext_copy_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Tag validation */

static bool tag_copy_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_ast_argument **arg ATTR_UNUSED, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	/* FIXME: currently not generated */
	*arg = sieve_ast_arguments_detach(*arg,1);
		
	return TRUE;
}

/* Command registration */

static const struct sieve_argument copy_tag = { 
	"days", NULL, 
	tag_copy_validate, 
	NULL, NULL 
};

/* Load extension into validator */
static bool ext_copy_validator_load(struct sieve_validator *validator)
{
	/* Register new command */
	//sieve_validator_register_tag(validator, cmd_reg, &copy_tag, -1); 	

	return TRUE;
}

