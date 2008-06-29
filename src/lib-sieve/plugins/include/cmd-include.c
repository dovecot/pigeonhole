#include "lib.h"

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

/* Forward declarations */

static bool opc_include_dump
	(const struct sieve_operation *op,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool opc_include_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_include_registered
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg);
static bool cmd_include_pre_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
		struct sieve_command_context *cmd);
static bool cmd_include_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_include_generate
	(struct sieve_generator *gentr,	struct sieve_command_context *ctx);

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
	cmd_include_pre_validate,  
	cmd_include_validate, 
	cmd_include_generate, 
	NULL 
};

/* Include operation */

const struct sieve_operation include_operation = { 
	"include",
	&include_extension,
	EXT_INCLUDE_OPERATION_INCLUDE,
	opc_include_dump, 
	opc_include_execute
};

/* Context structures */

struct cmd_include_context_data {
	enum ext_include_script_location location;
	bool location_assigned;
	struct sieve_script *script;
};   

/* Tags */

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

/* Tag validation */

static bool cmd_include_validate_location_tag
(struct sieve_validator *validator,	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{    
	struct cmd_include_context_data *ctx_data = 
		(struct cmd_include_context_data *) cmd->data;
	
	if ( ctx_data->location_assigned) {
		sieve_command_validate_error(validator, cmd, 
			"cannot use location tags ':personal' and ':global' multiple times "
			"for the include command");
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

/* Command registration */

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

static void cmd_include_ast_destroy
(struct sieve_ast *ast ATTR_UNUSED, struct sieve_ast_node *node)
{
	struct sieve_command_context *cmd = node->context;
	struct cmd_include_context_data *ctx_data = 
		(struct cmd_include_context_data *) cmd->data;
		
	sieve_script_unref(&ctx_data->script);
}

static const struct sieve_ast_node_object cmd_include_ast_object = {
	cmd_include_ast_destroy
};

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
	const char *script_path, *script_name;
	
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "value", 1, SAAT_STRING) ) {
		return FALSE;
	}
		
	/* Find the script */
	script_name = sieve_ast_argument_strc(arg);
	script_path = ext_include_get_script_path(ctx_data->location, script_name);
	if ( script_path == NULL )
		return FALSE;
	
	/* Create script object */
	if ( (script = sieve_script_create(script_path, script_name, 
		sieve_validator_error_handler(validator), NULL)) == NULL ) 
		return FALSE;	
		
	sieve_ast_link_object(cmd->ast_node, &cmd_include_ast_object);
	ctx_data->script = script;
	sieve_script_ref(script);	
		
	arg = sieve_ast_arguments_detach(arg, 1);
	
	return TRUE;
}

/*
 * Code Generation
 */
 
static bool cmd_include_generate
	(struct sieve_generator *gentr,	struct sieve_command_context *cmd) 
{
	struct sieve_binary *sbin = sieve_generator_get_binary(gentr);
	struct cmd_include_context_data *ctx_data = 
		(struct cmd_include_context_data *) cmd->data;
	unsigned int block_id;

	/* Compile (if necessary) and include the script into the binary.
	 * This yields the id of the binary block containing the compiled byte code.  
	 */
	if ( !ext_include_generate_include
		(gentr, cmd, ctx_data->location, ctx_data->script, &block_id) )
 		return FALSE;
 		
 	sieve_generator_emit_operation_ext	
		(gentr, &include_operation, ext_include_my_id);
	sieve_binary_emit_offset(sbin, block_id); 
 	 		
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool opc_include_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int block;
	
	if ( !sieve_binary_read_offset(denv->sbin, address, &block) )
		return FALSE;
		
	sieve_code_dumpf(denv, "INCLUDE [BLOCK: %d]", block);
	 
	return TRUE;
}

/* 
 * Code execution
 */
 
static bool opc_include_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	int block;
		
	if ( !sieve_binary_read_offset(renv->sbin, address, &block) )
		return FALSE;
	
	sieve_runtime_trace(renv, "INCLUDE command (BLOCK: %d)", block);
	
	return ext_include_execute_include(renv, (unsigned int) block);
}





