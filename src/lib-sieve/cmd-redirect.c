#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Forward declarations */

static bool cmd_redirect_opcode_dump
	(struct sieve_interpreter *interp ATTR_UNUSED, struct sieve_binary *sbin, 
		sieve_size_t *address);
static bool cmd_redirect_opcode_execute
	(struct sieve_interpreter *interp ATTR_UNUSED, struct sieve_binary *sbin, 
		sieve_size_t *address);

static bool cmd_redirect_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_redirect_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Redirect opcode */

const struct sieve_opcode cmd_redirect_opcode = 
	{ cmd_redirect_opcode_dump, cmd_redirect_opcode_execute };

/* Redirect command 
 * 
 * Syntax
 *   redirect <address: string>
 */

const struct sieve_command cmd_redirect = { 
	"redirect", 
	SCT_COMMAND,
	1, 0, FALSE, FALSE, 
	NULL, NULL,
	cmd_redirect_validate, 
	cmd_redirect_generate, 
	NULL 
};

/* Validation */

static bool cmd_redirect_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	/* Check argument */
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "address", 1, SAAT_STRING) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, arg);
	 
	return TRUE;
}

/*
 * Generation
 */
 
static bool cmd_redirect_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_opcode(generator, SIEVE_OPCODE_REDIRECT);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
	
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_redirect_opcode_dump
	(struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	printf("REDIRECT\n");

	return 
		sieve_opr_string_dump(sbin, address);
}

/*
 * Execution
 */

static bool cmd_redirect_opcode_execute
	(struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	string_t *redirect;

	t_push();

	if ( !sieve_opr_string_read(sbin, address, &redirect) ) {
		t_pop();
		return FALSE;
	}

	printf(">> REDIRECT \"%s\"\n", str_c(redirect));

	t_pop();
	return TRUE;
}
