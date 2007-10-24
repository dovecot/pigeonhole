#include <stdio.h>

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Forward declarations */
static bool ext_fileinto_validator_load(struct sieve_validator *validator);
static bool ext_fileinto_opcode_dump(struct sieve_interpreter *interpreter);
static bool ext_fileinto_opcode_execute(struct sieve_interpreter *interpreter); 


static bool cmd_fileinto_validate(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_fileinto_generate(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Extension definitions */
const struct sieve_extension fileinto_extension = 
	{ "fileinto", ext_fileinto_validator_load, NULL, 
		{ ext_fileinto_opcode_dump, ext_fileinto_opcode_execute } 
	};
static const struct sieve_command fileinto_command = 
	{ "fileinto", SCT_COMMAND, NULL, cmd_fileinto_validate, cmd_fileinto_generate, NULL };

/* Validation */

static bool cmd_fileinto_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
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
	
	cmd->data = (void *) arg;
	
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
	struct sieve_ast_argument *arg = (struct sieve_ast_argument *) ctx->data;
	
	sieve_generator_emit_ext_opcode(generator, &fileinto_extension);

	/* Emit folder string */  	
	if ( !sieve_generator_emit_string_argument(generator, arg) ) 
		return FALSE;
	
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool ext_fileinto_opcode_dump(struct sieve_interpreter *interpreter)
{
	printf("FILEINTO\n");
	sieve_interpreter_dump_operand(interpreter);
	
	return TRUE;
}

/*
 * Execution
 */

static bool ext_fileinto_opcode_execute
	(struct sieve_interpreter *interpreter ATTR_UNUSED) 
{
	return TRUE;
}

/*
static int sieve_fileinto(void *ac,
              void *ic ATTR_UNUSED,
              void *sc,
              void *mc,
              const char **errmsg ATTR_UNUSED)
{
    sieve_fileinto_context_t *fc = (sieve_fileinto_context_t *) ac;
    script_data_t *sd = (script_data_t *) sc;
    sieve_msgdata_t *md = (sieve_msgdata_t *) mc;
    enum mail_flags flags;
    const char *const *keywords;

    get_flags(fc->imapflags, &flags, &keywords);

    if (deliver_save(sd->namespaces, sd->storage_r, fc->mailbox,
             md->mail, flags, keywords) < 0)
        return SIEVE_FAIL;

    return SIEVE_OK;
}*/
