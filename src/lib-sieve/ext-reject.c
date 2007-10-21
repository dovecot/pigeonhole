#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-interpreter.h"

/* Forward declarations */

static bool cmd_reject_validate(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool ext_reject_opcode_dump(struct sieve_interpreter *interpreter, int opcode);

/* Extension definitions */

static const struct sieve_command reject_command = 
	{ "reject", SCT_COMMAND, NULL, cmd_reject_validate, NULL, NULL };
static const struct sieve_opcode_extension reject_opcode = 
	{ opc_reject_dump, NULL };

/* 
 * Validation 
 */

static bool cmd_reject_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg;
		
	/* Check valid syntax: 
	 *    reject <reason: string>
	 */
	if ( !sieve_validate_command_arguments(validator, cmd, 1, &arg) ||
		!sieve_validate_command_subtests(validator, cmd, 0) || 
	 	!sieve_validate_command_block(validator, cmd, FALSE, FALSE) ) {
	 	
		return FALSE;
	}
	
	cmd->data = arg;
	
	return TRUE;
}

/* Load extension into validator */
bool ext_reject_validator_load(struct sieve_validator *validator)
{
	/* Register new command */
	sieve_validator_register_command(validator, &reject_command);

	return TRUE;
}

/*
 * Generation
 */
 
bool cmd_reject_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	struct sieve_ast_argument *arg = (struct sieve_ast_argument *) ctx->data;
	
	sieve_generator_emit_opcode(generator, &reject_opcode);

	/* Emit reason string */  	
	if ( !sieve_generator_emit_string_argument(generator, arg) ) 
		return FALSE;
	
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool opc_reject_dump(struct sieve_interpreter *interpreter)
{
	printf("REJECT\n");
	sieve_interpreter_dump_operand(interpreter);
	
	return TRUE;
}

