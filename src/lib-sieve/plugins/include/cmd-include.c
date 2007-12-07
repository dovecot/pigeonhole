#include "lib.h"

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

#include "ext-include-common.h"

/* Forward declarations */

static bool opc_include_dump
	(const struct sieve_opcode *opcode,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool opc_include_execute
	(const struct sieve_opcode *opcode, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_include_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool cmd_include_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_include_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Include command 
 *	
 * Syntax: 
 *   include [LOCATION] <value: string>
 *
 * [LOCATION]:      
 *   ":personal" / ":global"
 */
const struct sieve_command cmd_include = { 
	"include",
	SCT_COMMAND, 
	1, 0, FALSE, FALSE, 
	cmd_include_registered,
	NULL,  
	cmd_include_validate, 
	cmd_include_generate, 
	NULL 
};

/* Include opcode */

const struct sieve_opcode include_opcode = { 
	"include",
	SIEVE_OPCODE_CUSTOM,
	&include_extension,
	0,
	opc_include_dump, 
	opc_include_execute
};

/* Tag validation */

static bool cmd_include_validate_location_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	/* SKELETON: Self destruct */
	*arg = sieve_ast_arguments_detach(*arg,1);
	
	return TRUE;
}

/* Command registration */

static const struct sieve_argument include_personal_tag = { 
	"personal", NULL, 
	cmd_include_validate_location_tag, 
	NULL, NULL 
};

static const struct sieve_argument include_global_tag = { 
	"global", NULL, 
	cmd_include_validate_location_tag, 
	NULL, NULL 
};

enum cmd_include_optional {
	OPT_END,
	OPT_LOCATION
};

static bool cmd_include_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag
		(validator, cmd_reg, &include_personal_tag, OPT_LOCATION); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &include_global_tag, OPT_LOCATION); 	

	return TRUE;
}

/* Command validation */

static bool cmd_include_validate(struct sieve_validator *validator, 
	struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg = cmd->first_positional;

	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "value", 1, SAAT_STRING) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, arg);	
	
	return TRUE;
}

/*
 * Generation
 */
 
static bool cmd_include_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool opc_include_dump
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	return TRUE;
}

/* 
 * Code execution
 */
 
static bool opc_include_execute
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	return TRUE;
}





