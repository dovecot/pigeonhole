/* Extension fileinto 
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC3028, draft-ietf-sieve-3028bis-13.txt
 * Implementation: full
 * Status: experimental, largely untested
 *
 */

#include <stdio.h>

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"
#include "sieve-result.h"

/* Forward declarations */

static bool ext_fileinto_load(int ext_id);
static bool ext_fileinto_validator_load(struct sieve_validator *validator);

static bool ext_fileinto_opcode_dump
	(const struct sieve_opcode *opcode, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool ext_fileinto_opcode_execute
	(const struct sieve_opcode *opcode, 
		const struct sieve_runtime_env *renv, sieve_size_t *address); 

static bool cmd_fileinto_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_fileinto_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Extension definitions */

static int ext_my_id;

const struct sieve_opcode fileinto_opcode;

const struct sieve_extension fileinto_extension = { 
	"fileinto", 
	ext_fileinto_load,
	ext_fileinto_validator_load, 
	NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_OPCODE(fileinto_opcode), 
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

/* Fileinto opcode */

const struct sieve_opcode fileinto_opcode = { 
	"FILEINTO",
	SIEVE_OPCODE_CUSTOM,
	&fileinto_extension,
	0,
	ext_fileinto_opcode_dump, 
	ext_fileinto_opcode_execute 
};

/* Validation */

static bool cmd_fileinto_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg = cmd->first_positional;
	
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "folder", 1, SAAT_STRING) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, cmd, arg, FALSE);
	
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
	sieve_generator_emit_opcode_ext(generator, &fileinto_opcode, ext_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
	
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool ext_fileinto_opcode_dump
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "FILEINTO");
	sieve_code_descend(denv);

	if ( !sieve_code_dumper_print_optional_operands(denv, address) )
		return FALSE;

	return 
		sieve_opr_string_dump(denv, address);
}

/*
 * Execution
 */

static bool ext_fileinto_opcode_execute
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_side_effects_list *slist = NULL; 
	string_t *folder;
	int ret = 0;
	
	if ( !sieve_interpreter_handle_optional_operands(renv, address, &slist) )
		return FALSE;

	t_push();
	
	if ( !sieve_opr_string_read(renv->sbin, address, &folder) ) {
		t_pop();
		return FALSE;
	}

	printf(">> FILEINTO \"%s\"\n", str_c(folder));

	ret = sieve_act_store_add_to_result(renv, slist, str_c(folder));

	t_pop();
	return ( ret >= 0 );
}





