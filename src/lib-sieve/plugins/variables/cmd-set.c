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
#include "ext-variables-modifiers.h"

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
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

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

/* Set operation 
 */
const struct sieve_operation cmd_set_operation = { 
	"SET",
	&variables_extension,
	EXT_VARIABLES_OPERATION_SET,
	cmd_set_operation_dump, 
	cmd_set_operation_execute
};

/* Compiler context */

struct cmd_set_modifier_context {
	const struct sieve_variables_modifier *modifier;
	int ext_id;
};

struct cmd_set_context {
	ARRAY_DEFINE(modifiers, struct cmd_set_modifier_context *);
};

/* 
 * Set modifier tag
 *
 * [MODIFIER]:
 *   ":lower" / ":upper" / ":lowerfirst" / ":upperfirst" /
 *             ":quotewildcard" / ":length"
 */

/* Forward declarations */
 
static bool tag_modifier_is_instance_of
	(struct sieve_validator *validator, struct sieve_command_context *cmdctx,	
		struct sieve_ast_argument *arg);	
static bool tag_modifier_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);

/* Modifier tag object */

const struct sieve_argument modifier_tag = { 
	"MODIFIER",
	tag_modifier_is_instance_of, 
	NULL,
	tag_modifier_validate, 
	NULL, NULL
};
 
/* Modifier tag implementation */ 
 
static bool tag_modifier_is_instance_of
(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_command_context *cmdctx,	
	struct sieve_ast_argument *arg)
{	
	int ext_id;
	const struct sieve_variables_modifier *modf = ext_variables_modifier_find
		(validator, sieve_ast_argument_tag(arg), &ext_id);
		
	if ( modf != NULL ) {
		pool_t pool = sieve_command_pool(cmdctx);
		
		struct cmd_set_modifier_context *ctx = 
			p_new(pool, struct cmd_set_modifier_context, 1);
		ctx->modifier = modf;
		ctx->ext_id = ext_id;
		
		arg->context = ctx;
		return TRUE;
	}
		
	return FALSE;
}

static bool tag_modifier_validate
(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	unsigned int i;
	bool inserted;
	struct cmd_set_modifier_context *mctx = (*arg)->context;
	const struct sieve_variables_modifier *modf = mctx->modifier;
	struct cmd_set_context *sctx = (struct cmd_set_context *) cmd->data;
	
	inserted = FALSE;
	for ( i = 0; i < array_count(&sctx->modifiers) && !inserted; i++ ) {
		struct cmd_set_modifier_context * const *smctx =
			array_idx(&sctx->modifiers, i);
		const struct sieve_variables_modifier *smdf = (*smctx)->modifier;
	
		if ( smdf->precedence == modf->precedence ) {
			sieve_command_validate_error(validator, cmd, 
				"modifiers :%s and :%s specified for the set command conflict "
				"having equal precedence", 
				smdf->object.identifier, modf->object.identifier);
			return FALSE;
		}
			
		if ( smdf->precedence < modf->precedence ) {
			array_insert(&sctx->modifiers, i, &mctx, 1);
			inserted = TRUE;
		}
	}
	
	if ( !inserted )
		array_append(&sctx->modifiers, &mctx, 1);
	
	/* Added to modifier list; self-destruct to prevent duplicate generation */
	*arg = sieve_ast_arguments_detach(*arg, 1);
	
	return TRUE;
}

/* Command registration */

static bool cmd_set_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag(validator, cmd_reg, &modifier_tag, 0); 	

	return TRUE;
}

/* Command validation */

static bool cmd_set_pre_validate
(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_command_context *cmd)
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
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	struct sieve_binary *sbin = cgenv->sbin;
	struct cmd_set_context *sctx = (struct cmd_set_context *) ctx->data;
	unsigned int i;	

	sieve_operation_emit_code
		(sbin, &cmd_set_operation, ext_variables_my_id); 

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
		return FALSE;	
		
	/* Generate modifiers (already sorted during validation) */
	sieve_binary_emit_byte(sbin, array_count(&sctx->modifiers));
	for ( i = 0; i < array_count(&sctx->modifiers); i++ ) {
		struct cmd_set_modifier_context * const * mctx =
			array_idx(&sctx->modifiers, i);
			
		ext_variables_opr_modifier_emit(sbin, (*mctx)->modifier, (*mctx)->ext_id);
	}

	return TRUE;
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
		if ( !ext_variables_opr_modifier_dump(denv, address) )
			return FALSE;
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
	
	sieve_runtime_trace(renv, "SET action");
	
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
				const struct sieve_variables_modifier *modf =
					ext_variables_opr_modifier_read(renv, address);

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






