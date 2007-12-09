#include "lib.h"

#include "sieve-common.h"

#include "sieve-script.h"
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
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg);
static bool cmd_include_pre_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
		struct sieve_command_context *cmd);
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
	cmd_include_pre_validate,  
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

/* Context structures */

struct cmd_include_context_data {
	enum { LOCATION_PERSONAL, LOCATION_GLOBAL } location;
	bool location_assigned;
	const char *script_name;
	const char *script_path;
};   

/* Tags */

static bool cmd_include_validate_location_tag
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);

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
		ctx_data->location = LOCATION_PERSONAL;
	else if ( (*arg)->argument == &include_global_tag )
		ctx_data->location = LOCATION_GLOBAL;
	else
		return FALSE;
	
	ctx_data->location_assigned = TRUE;

	/* Delete this tag (for now) */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	return TRUE;
}

/* Command registration */

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

static bool cmd_include_pre_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
		struct sieve_command_context *cmd)
{
	struct cmd_include_context_data *ctx_data;

	/* Assign context */
	ctx_data = p_new(sieve_command_pool(cmd), struct cmd_include_context_data, 1);
	ctx_data->location = LOCATION_PERSONAL;
	cmd->data = ctx_data;
	
	return TRUE;
}

static bool cmd_include_validate(struct sieve_validator *validator, 
	struct sieve_command_context *cmd) 
{ 	
	pool_t pool = sieve_command_pool(cmd);
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct cmd_include_context_data *ctx_data = 
		(struct cmd_include_context_data *) cmd->data;
	
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "value", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	/* Get script path */

	ctx_data->script_name = sieve_ast_argument_strc(arg);
	
	/* FIXME: Hardcoded */
#define HARDCODED_DIR "src/lib-sieve/plugins/include/"
	if ( ctx_data->location == LOCATION_PERSONAL )
		ctx_data->script_path = p_strconcat
  		(pool, HARDCODED_DIR, ctx_data->script_name, ".sieve", NULL);
	else if ( ctx_data->location == LOCATION_GLOBAL )
		ctx_data->script_path = p_strconcat
  		(pool, HARDCODED_DIR, ctx_data->script_name, ".sieve", NULL);
	else 
		return FALSE;
		
	arg = sieve_ast_arguments_detach(arg, 1);
	
	return TRUE;
}

/*
 * Code Generation
 */
 
static bool cmd_include_generate
	(struct sieve_generator *generator,	struct sieve_command_context *cmd) 
{
	struct cmd_include_context_data *ctx_data = 
		(struct cmd_include_context_data *) cmd->data;

	/* Generate */
	if ( !ext_include_generate_include
		(generator, cmd, ctx_data->script_path, ctx_data->script_name) ) {
 		return FALSE;
 	}
 	 		
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





