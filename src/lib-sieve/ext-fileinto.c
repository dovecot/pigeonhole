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
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool ext_fileinto_opcode_execute
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address); 

static bool cmd_fileinto_validate(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_fileinto_generate(struct sieve_generator *generator,	struct sieve_command_context *ctx);

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

static const struct sieve_command fileinto_command = 
	{ "fileinto", SCT_COMMAND, NULL, cmd_fileinto_validate, cmd_fileinto_generate, NULL };

static bool ext_fileinto_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

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
	struct sieve_binary *sbin ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
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
