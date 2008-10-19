/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-binary.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-include-common.h"
#include "ext-include-binary.h"

/* 
 * Include command 
 *	
 * Syntax: 
 *   include [LOCATION] <value: string>
 *
 * [LOCATION]:      
 *   ":personal" / ":global"
 */

static bool cmd_include_registered
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg);
static bool cmd_include_pre_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
		struct sieve_command_context *cmd);
static bool cmd_include_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_include_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command cmd_include = { 
	"include",
	SCT_COMMAND, 
	1, 0, FALSE, FALSE, 
	cmd_include_registered,
	cmd_include_pre_validate,  
	cmd_include_validate, 
	cmd_include_generate, 
	NULL 
};

/* 
 * Include operation 
 */

static bool opc_include_dump
	(const struct sieve_operation *op,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int opc_include_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation include_operation = { 
	"include",
	&include_extension,
	EXT_INCLUDE_OPERATION_INCLUDE,
	opc_include_dump, 
	opc_include_execute
};

/* 
 * Context structures 
 */

struct cmd_include_context_data {
	enum ext_include_script_location location;
	bool location_assigned;
	struct sieve_script *script;
};   

/* 
 * Tagged arguments
 */

static bool cmd_include_validate_location_tag
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);

static const struct sieve_argument include_personal_tag = { 
	"personal", 
	NULL, NULL,
	cmd_include_validate_location_tag, 
	NULL, NULL 
};

static const struct sieve_argument include_global_tag = { 
	"global", 
	NULL, NULL,
	cmd_include_validate_location_tag, 
	NULL, NULL 
};

/* 
 * Tag validation 
 */

static bool cmd_include_validate_location_tag
(struct sieve_validator *validator,	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{    
	struct cmd_include_context_data *ctx_data = 
		(struct cmd_include_context_data *) cmd->data;
	
	if ( ctx_data->location_assigned) {
		sieve_argument_validate_error(validator, *arg, 
			"include: cannot use location tags ':personal' and ':global' "
			"multiple times");
		return FALSE;
	}
	
	if ( (*arg)->argument == &include_personal_tag )
		ctx_data->location = EXT_INCLUDE_LOCATION_PERSONAL;
	else if ( (*arg)->argument == &include_global_tag )
		ctx_data->location = EXT_INCLUDE_LOCATION_GLOBAL;
	else
		return FALSE;
	
	ctx_data->location_assigned = TRUE;

	/* Delete this tag (for now) */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	return TRUE;
}

/* 
 * Command registration 
 */

static bool cmd_include_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag
		(validator, cmd_reg, &include_personal_tag, 0); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &include_global_tag, 0); 	

	return TRUE;
}

/* 
 * Command validation 
 */

static bool cmd_include_pre_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
		struct sieve_command_context *cmd)
{
	struct cmd_include_context_data *ctx_data;

	/* Assign context */
	ctx_data = p_new(sieve_command_pool(cmd), struct cmd_include_context_data, 1);
	ctx_data->location = EXT_INCLUDE_LOCATION_PERSONAL;
	cmd->data = ctx_data;
	
	return TRUE;
}

static bool cmd_include_validate(struct sieve_validator *validator, 
	struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct cmd_include_context_data *ctx_data = 
		(struct cmd_include_context_data *) cmd->data;
	struct sieve_script *script;
	const char *script_dir, *script_name;
	
	/* Check argument */
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "value", 1, SAAT_STRING) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(validator, cmd, arg, FALSE) )
		return FALSE;

	/* FIXME: We can currently only handle string literal argument, so
	 * variables are not allowed.
	 */
	if ( !sieve_argument_is_string_literal(arg) ) {
		sieve_argument_validate_error(validator, arg, 
			"this Sieve implementation currently only supports "
			"a literal string argument for the include command");
		return FALSE;
	}

	/* Find the script */

	script_name = sieve_ast_argument_strc(arg);

	if ( strchr(script_name, '/') != NULL ) {
 		sieve_argument_validate_error(validator, arg,
			"include: '/' not allowed in script name (%s)",
			str_sanitize(script_name, 80));
		return FALSE;
	}
		
	script_dir = ext_include_get_script_directory
		(ctx_data->location, script_name);
	if ( script_dir == NULL ) {
		sieve_argument_validate_error(validator, arg,
			"include: specified location for included script '%s' is unavailable "
			"(system logs should provide more information)",
			str_sanitize(script_name, 80));
		return FALSE;
	}
	
	/* Create script object */
	script = sieve_script_create_in_directory(script_dir, script_name, 
		sieve_validator_error_handler(validator), NULL);
	if ( script == NULL ) 
		return FALSE;	

	ext_include_ast_link_included_script(cmd->ast_node->ast, script);		
	ctx_data->script = script;
		
	arg = sieve_ast_arguments_detach(arg, 1);
	
	return TRUE;
}

/*
 * Code Generation
 */
 
static bool cmd_include_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *cmd) 
{
	struct cmd_include_context_data *ctx_data = 
		(struct cmd_include_context_data *) cmd->data;
	const struct ext_include_script_info *included;

	/* Compile (if necessary) and include the script into the binary.
	 * This yields the id of the binary block containing the compiled byte code.  
	 */
	if ( !ext_include_generate_include
		(cgenv, cmd, ctx_data->location, ctx_data->script, &included) )
 		return FALSE;
 		
 	(void)sieve_operation_emit_code(cgenv->sbin, &include_operation);
	(void)sieve_binary_emit_unsigned(cgenv->sbin, included->id); 
 	 		
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool opc_include_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct ext_include_script_info *included;
	struct ext_include_binary_context *binctx;
	unsigned int include_id;

	sieve_code_dumpf(denv, "INCLUDE:");
	
	sieve_code_mark(denv);
	if ( !sieve_binary_read_unsigned(denv->sbin, address, &include_id) )
		return FALSE;

	binctx = ext_include_binary_get_context(denv->sbin);
	included = ext_include_binary_script_get_included(binctx, include_id);
	if ( included == NULL )
		return FALSE;
		
	sieve_code_descend(denv);
	sieve_code_dumpf(denv, "script: %s [ID: %d, BLOCK: %d]", 
		sieve_script_filename(included->script), include_id, included->block_id);
	 
	return TRUE;
}

/* 
 * Execution
 */
 
static int opc_include_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	unsigned int include_id;
		
	if ( !sieve_binary_read_unsigned(renv->sbin, address, &include_id) ) {
		sieve_runtime_trace_error(renv, "invalid include-id operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	return ext_include_execute_include(renv, include_id);
}





