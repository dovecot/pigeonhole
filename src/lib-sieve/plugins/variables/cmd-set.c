#include "lib.h"

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

#include "ext-variables-common.h"

/* Forward declarations */

static bool cmd_set_operation_dump
	(const struct sieve_operation *op,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool cmd_set_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_set_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool cmd_set_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_set_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Set command 
 *	
 * Syntax: 
 *    set [MODIFIER] <name: string> <value: string>
 */
const struct sieve_command cmd_set = { 
	"set",
	SCT_COMMAND, 
	2, 0, FALSE, FALSE, 
	cmd_set_registered,
	NULL,  
	cmd_set_validate, 
	cmd_set_generate, 
	NULL 
};

/* Set operation */
const struct sieve_operation cmd_set_operation = { 
	"SET",
	&variables_extension,
	EXT_VARIABLES_OPERATION_SET,
	cmd_set_operation_dump, 
	cmd_set_operation_execute
};

/* Tag validation */

/* [MODIFIER]:
 *   ":lower" / ":upper" / ":lowerfirst" / ":upperfirst" /
 *             ":quotewildcard" / ":length"
 *
 * FIXME: Provide support to add further modifiers (as needed by notify) 
 */
 
static bool tag_modifier_is_instance_of
(struct sieve_validator *validator, 
	struct sieve_command_context *cmdctx ATTR_UNUSED,	
	struct sieve_ast_argument *arg)
{
	return ext_variables_set_modifier_find
		(validator, sieve_ast_argument_tag(arg)) != NULL;
}

static bool tag_modifier_validate
(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);
	
	return TRUE;
}

static bool tag_modifier_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	return TRUE;
}

const struct sieve_argument modifier_tag = { 
	"MODIFIER",
	tag_modifier_is_instance_of, 
	tag_modifier_validate, 
	NULL,
	tag_modifier_generate 
};

/* Pre-defined modifiers */

const struct ext_variables_set_modifier lower_modifier = {
	"lower", 
	EXT_VARIABLES_SET_MODIFIER_LOWER,
	40
};

const struct ext_variables_set_modifier upper_modifier = {
	"upper", 
	EXT_VARIABLES_SET_MODIFIER_UPPER,
	40
};

const struct ext_variables_set_modifier lowerfirst_modifier = {
	"lowerfirst", 
	EXT_VARIABLES_SET_MODIFIER_LOWERFIRST,
	30
};

const struct ext_variables_set_modifier upperfirst_modifier = {
	"upperfirst", 
	EXT_VARIABLES_SET_MODIFIER_UPPERFIRST,
	30
};

const struct ext_variables_set_modifier quotewildcard_modifier = {
	"quotewildcard",
	EXT_VARIABLES_SET_MODIFIER_QUOTEWILDCARD,
	20
};

const struct ext_variables_set_modifier length_modifier = {
	"length", 
	EXT_VARIABLES_SET_MODIFIER_LENGTH,
	10
};

/* Command registration */

static bool cmd_set_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag(validator, cmd_reg, &modifier_tag, 0); 	

	return TRUE;
}

/* Command validation */

static bool cmd_set_validate(struct sieve_validator *validator, 
	struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg = cmd->first_positional;
	
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "name", 1, SAAT_STRING) ) {
		return FALSE;
	}
	ext_variables_variable_argument_activate(validator, arg);

	arg = sieve_ast_argument_next(arg);
	
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "value", 2, SAAT_STRING) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, cmd, arg, FALSE);	
	
	return TRUE;
}

/*
 * Generation
 */
 
static bool cmd_set_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_operation_ext
		(generator, &cmd_set_operation, ext_variables_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_set_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{	
	sieve_code_dumpf(denv, "SET");
	sieve_code_descend(denv);

	return 
		sieve_opr_string_dump(denv, address) &&
		sieve_opr_string_dump(denv, address);
}

/* 
 * Code execution
 */
 
static bool cmd_set_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	struct sieve_variable_storage *storage;
	unsigned int var_index;
	string_t *value;
	
	printf(">> SET\n");
	
	if ( !ext_variables_opr_variable_read(renv, address, &storage, &var_index) )
		return FALSE;
		
	if ( !sieve_opr_string_read(renv, address, &value) )
		return FALSE;
		
	sieve_variable_assign(storage, var_index, value);
		
	return TRUE;
}





