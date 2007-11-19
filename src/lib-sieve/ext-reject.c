
#include <stdio.h>

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-interpreter.h"

/* Forward declarations */

static bool ext_reject_load(int ext_id);
static bool ext_reject_validator_load(struct sieve_validator *validator);
static bool ext_reject_generator_load(struct sieve_generator *generator);

static bool cmd_reject_validate(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_reject_generate(struct sieve_generator *generator,	struct sieve_command_context *ctx); 

static bool ext_reject_opcode_dump
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);

/* Extension definitions */

static int ext_my_id;

struct sieve_opcode reject_opcode = 
	{ ext_reject_opcode_dump, NULL };
	
struct sieve_extension reject_extension = { 
	"reject", 
	ext_reject_load,
	ext_reject_validator_load, 
	ext_reject_generator_load, 
	NULL, 
	&reject_opcode, 
	NULL 
};

static const struct sieve_command reject_command = { 
	"reject", 
	SCT_COMMAND, 
	NULL, 
	cmd_reject_validate, 
	cmd_reject_generate, 
	NULL 
};

static bool ext_reject_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* 
 * Validation 
 */

static bool cmd_reject_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg;
		
	/* Check valid syntax: 
	 *    reject <reason: string>
	 */
	if ( !sieve_validate_command_arguments(validator, cmd, 1) ||
		!sieve_validate_command_subtests(validator, cmd, 0) || 
	 	!sieve_validate_command_block(validator, cmd, FALSE, FALSE) ) {
	 	
		return FALSE;
	}

	arg = cmd->first_positional;
		
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "reason", 1, SAAT_STRING) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, arg);
	
	return TRUE;
}

/* Load extension into validator */
static bool ext_reject_validator_load(struct sieve_validator *validator)
{
	/* Register new command */
	sieve_validator_register_command(validator, &reject_command);

	return TRUE;
}

/*
 * Generation
 */
 
static bool cmd_reject_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_opcode_ext(generator, ext_my_id);

	/* Generate arguments */
    if ( !sieve_generate_arguments(generator, ctx, NULL) )
        return FALSE;
	
	return TRUE;
}

/* Load extension into generator */
static bool ext_reject_generator_load(struct sieve_generator *generator ATTR_UNUSED)
{
	return TRUE;
}


/* 
 * Code dump
 */
 
static bool ext_reject_opcode_dump
	(struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	printf("REJECT\n");
	
	return
		sieve_opr_string_dump(sbin, address);
}

