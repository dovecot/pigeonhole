#include "lib.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-extensions-private.h"

#include "sieve-code.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-binary.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-variables-common.h"

#include <ctype.h>

/* Forward declarations */

static bool cmd_set_operation_dump
	(const struct sieve_operation *op,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool cmd_set_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_set_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool cmd_set_pre_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
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
	cmd_set_pre_validate,  
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

/* Compiler context */
struct cmd_set_context {
	ARRAY_DEFINE(modifiers, struct ext_variables_set_modifier *);
	
};

/* Tag validation */

/* [MODIFIER]:
 *   ":lower" / ":upper" / ":lowerfirst" / ":upperfirst" /
 *             ":quotewildcard" / ":length"
 *
 * FIXME: Provide support to add further modifiers (as needed by notify) 
 */
 
static bool tag_modifier_is_instance_of
(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_command_context *cmdctx ATTR_UNUSED,	
	struct sieve_ast_argument *arg)
{
	arg->context = (void *) ext_variables_set_modifier_find
		(validator, sieve_ast_argument_tag(arg));
		
	return arg->context != NULL;
}

static bool tag_modifier_validate
(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	unsigned int i;
	bool inserted;
	struct ext_variables_set_modifier *smodf = (*arg)->context;
	struct cmd_set_context *sctx = (struct cmd_set_context *) cmd->data;
	
	inserted = FALSE;
	for ( i = 0; i < array_count(&sctx->modifiers) && !inserted; i++ ) {
		struct ext_variables_set_modifier * const * mdf =
			array_idx(&sctx->modifiers, i);
	
		if ( (*mdf)->precedence == smodf->precedence ) {
			sieve_command_validate_error(validator, cmd, 
				"modifiers :%s and :%s specified for the set command conflict with "
				"equal precedence", (*mdf)->identifier, smodf->identifier);
			return FALSE;
		}
			
		if ( (*mdf)->precedence < smodf->precedence ) {
			array_insert(&sctx->modifiers, i, &smodf, 1);
			inserted = TRUE;
		}
	}
	
	if ( !inserted )
		array_append(&sctx->modifiers, &smodf, 1);
	
	/* Added to modifier list; self-destruct to prevent duplicate generation */
	*arg = sieve_ast_arguments_detach(*arg, 1);
	
	return TRUE;
}

const struct sieve_argument modifier_tag = { 
	"MODIFIER",
	tag_modifier_is_instance_of, 
	NULL,
	tag_modifier_validate, 
	NULL, NULL
};

/* Pre-defined modifiers */

bool mod_lower_modify(string_t *in, string_t **result);
bool mod_upper_modify(string_t *in, string_t **result);
bool mod_lowerfirst_modify(string_t *in, string_t **result);
bool mod_upperfirst_modify(string_t *in, string_t **result);
bool mod_length_modify(string_t *in, string_t **result);
bool mod_quotewildcard_modify(string_t *in, string_t **result);

const struct ext_variables_set_modifier lower_modifier = {
	"lower", 
	EXT_VARIABLES_SET_MODIFIER_LOWER,
	40,
	mod_lower_modify
};

const struct ext_variables_set_modifier upper_modifier = {
	"upper", 
	EXT_VARIABLES_SET_MODIFIER_UPPER,
	40,
	mod_upper_modify
};

const struct ext_variables_set_modifier lowerfirst_modifier = {
	"lowerfirst", 
	EXT_VARIABLES_SET_MODIFIER_LOWERFIRST,
	30,
	mod_lowerfirst_modify
};

const struct ext_variables_set_modifier upperfirst_modifier = {
	"upperfirst", 
	EXT_VARIABLES_SET_MODIFIER_UPPERFIRST,
	30,
	mod_upperfirst_modify
};

const struct ext_variables_set_modifier quotewildcard_modifier = {
	"quotewildcard",
	EXT_VARIABLES_SET_MODIFIER_QUOTEWILDCARD,
	20,
	mod_quotewildcard_modify
};

const struct ext_variables_set_modifier length_modifier = {
	"length", 
	EXT_VARIABLES_SET_MODIFIER_LENGTH,
	10,
	mod_length_modify
};

const struct ext_variables_set_modifier *core_modifiers[] = {
	&lower_modifier,
	&upper_modifier,
	&lowerfirst_modifier,
	&upperfirst_modifier,
	&quotewildcard_modifier,
	&length_modifier
};

static struct sieve_extension_obj_registry setmodf_default_reg =
	SIEVE_EXT_DEFINE_SET_MODIFIERS(core_modifiers);

/* Command registration */

static bool cmd_set_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag(validator, cmd_reg, &modifier_tag, 0); 	

	return TRUE;
}

/* Command validation */

static bool cmd_set_pre_validate
	(struct sieve_validator *validator ATTR_UNUSED, struct sieve_command_context *cmd)
{
	pool_t pool = sieve_command_pool(cmd);
	struct cmd_set_context *sctx = p_new(pool, struct cmd_set_context, 1);
	
	/* Create an array for the sorted list of modifiers */
	p_array_init(&sctx->modifiers, pool, 2);

	cmd->data = (void *) sctx;
	
	return TRUE;
} 

static bool cmd_set_validate(struct sieve_validator *validator, 
	struct sieve_command_context *cmd) 
{ 
	struct sieve_ast_argument *arg = cmd->first_positional;
		
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "name", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	if ( !sieve_variable_argument_activate(validator, cmd, arg, TRUE) ) {
		return FALSE;
	}

	arg = sieve_ast_argument_next(arg);
	
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "value", 2, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(validator, cmd, arg, FALSE);	
}

/*
 * Generation
 */
 
static bool cmd_set_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	struct cmd_set_context *sctx = (struct cmd_set_context *) ctx->data;
	unsigned int i;	

	sieve_generator_emit_operation_ext
		(generator, &cmd_set_operation, ext_variables_my_id); 

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;	
		
	/* Generate modifiers (already sorted during validation) */
	sieve_binary_emit_byte(sbin, array_count(&sctx->modifiers));
	for ( i = 0; i < array_count(&sctx->modifiers); i++ ) {
		struct ext_variables_set_modifier * const * smodf =
			array_idx(&sctx->modifiers, i);
			
		sieve_binary_emit_byte(sbin, (*smodf)->code);
	}

	return TRUE;
}

/* 
 * Modifier access
 */
 
static const struct sieve_extension_obj_registry *
	cmd_set_modifier_registry_get
(struct sieve_binary *sbin, unsigned int ext_index)
{
	int ext_id = -1; 
	const struct sieve_variables_extension *ext;
	
	if ( sieve_binary_extension_get_by_index(sbin, ext_index, &ext_id) == NULL )
		return NULL;

	if ( (ext=sieve_variables_extension_get(sbin, ext_id)) == NULL ) 
		return NULL;
		
	return &(ext->set_modifiers);
}

static const struct ext_variables_set_modifier *cmd_set_modifier_read
	(struct sieve_binary *sbin, sieve_size_t *address) 
{
	return sieve_extension_read_obj
		(struct ext_variables_set_modifier, sbin, address, &setmodf_default_reg, 
			cmd_set_modifier_registry_get);
}

/* 
 * Code dump
 */
 
static bool cmd_set_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{	
	unsigned int mdfs, i;
	
	sieve_code_dumpf(denv, "SET");
	sieve_code_descend(denv);
	
	/* Print both variable name and string value */
	if ( !sieve_opr_string_dump(denv, address) ||
		!sieve_opr_string_dump(denv, address) )
		return FALSE;
	
	/* Read the number of applied modifiers we need to read */
	if ( !sieve_binary_read_byte(denv->sbin, address, &mdfs) ) 
		return FALSE;
	
	/* Print all modifiers (sorted during code generation already) */
	for ( i = 0; i < mdfs; i++ ) {
		const struct ext_variables_set_modifier *modf =
			cmd_set_modifier_read(denv->sbin, address);
		
		if ( modf == NULL )
			return FALSE;
			
		sieve_code_dumpf(denv, "MOD: %s", modf->identifier);
	}
	
	return TRUE;
}

/* 
 * Code execution
 */
 
static bool cmd_set_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	struct sieve_variable_storage *storage;
	unsigned int var_index, mdfs, i;
	string_t *value;
	
	printf(">> SET\n");
	
	/* Read the variable */
	if ( !sieve_variable_operand_read
		(renv, address, &storage, &var_index) )
		return FALSE;
		
	/* Read the raw string value */
	if ( !sieve_opr_string_read(renv, address, &value) )
		return FALSE;
		
	/* Read the number of modifiers used */
	if ( !sieve_binary_read_byte(renv->sbin, address, &mdfs) ) 
		return FALSE;
	
	/* Determine and assign the value */
	T_BEGIN {
		/* Apply modifiers if necessary (sorted during code generation already) */
		if ( str_len(value) > 0 ) {
			for ( i = 0; i < mdfs; i++ ) {
				string_t *new_value;
				const struct ext_variables_set_modifier *modf;
				
				modf = cmd_set_modifier_read(renv->sbin, address);
				if ( modf == NULL ) {
					value = NULL;
					break;
				}
				
				if ( modf->modify != NULL ) {
					if ( !modf->modify(value, &new_value) ) {
						value = NULL;
						break;
					}
				
					value = new_value;
				}
			}
		}	
		
		/* Actually assign the value if all is well */
		if ( value != NULL ) {
			sieve_variable_assign(storage, var_index, value);
		}	
	} T_END;
			
	return ( value != NULL );
}

/* Pre-defined modifier implementations */

bool mod_upperfirst_modify(string_t *in, string_t **result)
{
	char *content;
	
	*result = t_str_new(str_len(in));
	str_append_str(*result, in);
		
	content = str_c_modifiable(*result);
	content[0] = toupper(content[0]);

	return TRUE;
}

bool mod_lowerfirst_modify(string_t *in, string_t **result)
{
	char *content;
	
	*result = t_str_new(str_len(in));
	str_append_str(*result, in);
		
	content = str_c_modifiable(*result);
	content[0] = i_tolower(content[0]);

	return TRUE;
}

bool mod_upper_modify(string_t *in, string_t **result)
{
	char *content;
	
	*result = t_str_new(str_len(in));
	str_append_str(*result, in);

	content = str_c_modifiable(*result);
	content = str_ucase(content);
	
	return TRUE;
}

bool mod_lower_modify(string_t *in, string_t **result)
{
	char *content;
	
	*result = t_str_new(str_len(in));
	str_append_str(*result, in);

	content = str_c_modifiable(*result);
	content = str_lcase(content);

	return TRUE;
}

bool mod_length_modify(string_t *in, string_t **result)
{
	*result = t_str_new(64);
	str_printfa(*result, "%d", str_len(in));

	return TRUE;
}

bool mod_quotewildcard_modify(string_t *in, string_t **result)
{
	unsigned int i;
	const char *content;
	
	*result = t_str_new(str_len(in) * 2);
	content = (const char *) str_data(in);
	
	for ( i = 0; i < str_len(in); i++ ) {
		if ( content[i] == '*' || content[i] == '?' || content[i] == '\\' ) {
			str_append_c(*result, '\\');
		}
		str_append_c(*result, content[i]);
	}
	
	return TRUE;
}






