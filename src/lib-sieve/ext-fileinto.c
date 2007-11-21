/* Extension fileinto 
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC3028
 * Implementation: validation, generation and interpretation, no actual 
 *   execution. 
 * Status: experimental, largely untested
 *
 */

#include <stdio.h>

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Forward declarations */

static bool ext_fileinto_load(int ext_id);
static bool ext_fileinto_validator_load(struct sieve_validator *validator);

static bool ext_fileinto_opcode_dump
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, 
		sieve_size_t *address);
static bool ext_fileinto_opcode_execute
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, 
		sieve_size_t *address); 

static bool cmd_fileinto_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_fileinto_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Extension definitions */

static int ext_my_id;

const struct sieve_opcode fileinto_opcode = 
	{ ext_fileinto_opcode_dump, ext_fileinto_opcode_execute };

const struct sieve_extension fileinto_extension = { 
	"fileinto", 
	ext_fileinto_load,
	ext_fileinto_validator_load, 
	NULL, 
	NULL, 
	&fileinto_opcode, 
	NULL	
};

static bool ext_fileinto_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* Fileinto command
 *
 * Syntax: 
 *   fileinto <folder: string>
 */
static const struct sieve_command fileinto_command = { 
	"fileinto", 
	SCT_COMMAND,
	1, 0, FALSE, FALSE, 
	NULL, NULL,
	cmd_fileinto_validate, 
	cmd_fileinto_generate, 
	NULL 
};

/* Validation */

static bool cmd_fileinto_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg = cmd->first_positional;
	
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "folder", 1, SAAT_STRING) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, arg);
	
	return TRUE;
}

/* Load extension into validator */
static bool ext_fileinto_validator_load(struct sieve_validator *validator)
{
	/* Register new command */
	sieve_validator_register_command(validator, &fileinto_command);

	return TRUE;
}

/*
 * Generation
 */
 
static bool cmd_fileinto_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_opcode_ext(generator, ext_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
	
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool ext_fileinto_opcode_dump
	(struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	printf("FILEINTO\n");

	return 
		sieve_opr_string_dump(sbin, address);
}

/*
 * Execution
 */

static bool ext_fileinto_opcode_execute
	(struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, 
	sieve_size_t *address)
{
	string_t *folder;

	t_push();

	if ( !sieve_opr_string_read(sbin, address, &folder) ) {
		t_pop();
		return FALSE;
	}

	printf(">> FILEINTO \"%s\"\n", str_c(folder));

	t_pop();
	return TRUE;
}
