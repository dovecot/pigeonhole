/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-extensions.h"

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

/* 
 * Set command 
 *	
 * Syntax: 
 *    set [MODIFIER] <name: string> <value: string>
 */

static bool cmd_set_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool cmd_set_pre_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_set_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_set_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

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

/* 
 * Set operation 
 */

static bool cmd_set_operation_dump
	(const struct sieve_operation *op,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_set_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation cmd_set_operation = { 
	"SET",
	&variables_extension,
	EXT_VARIABLES_OPERATION_SET,
	cmd_set_operation_dump, 
	cmd_set_operation_execute
};

/* 
 * Compiler context 
 */

struct cmd_set_context {
	ARRAY_DEFINE(modifiers, const struct sieve_variables_modifier *);
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
	struct sieve_command_context *cmdctx ATTR_UNUSED,	
	struct sieve_ast_argument *arg)
{	
	const struct sieve_variables_modifier *modf = ext_variables_modifier_find
		(validator, sieve_ast_argument_tag(arg));

	arg->context = (void *) modf;
		
	return ( modf != NULL );
}

static bool tag_modifier_validate
(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	unsigned int i;
	bool inserted;
	const struct sieve_variables_modifier *modf = 
		(const struct sieve_variables_modifier *) (*arg)->context;
	struct cmd_set_context *sctx = (struct cmd_set_context *) cmd->data;
	
	inserted = FALSE;
	for ( i = 0; i < array_count(&sctx->modifiers) && !inserted; i++ ) {
		const struct sieve_variables_modifier * const *smdf =
			array_idx(&sctx->modifiers, i);
	
		if ( (*smdf)->precedence == modf->precedence ) {
			sieve_argument_validate_error(validator, *arg, 
				"modifiers :%s and :%s specified for the set command conflict "
				"having equal precedence", 
				(*smdf)->object.identifier, modf->object.identifier);
			return FALSE;
		}
			
		if ( (*smdf)->precedence < modf->precedence ) {
			array_insert(&sctx->modifiers, i, &modf, 1);
			inserted = TRUE;
		}
	}
	
	if ( !inserted )
		array_append(&sctx->modifiers, &modf, 1);
	
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

/* 
 * Command validation 
 */

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
 * Code generation
 */
 
static bool cmd_set_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	struct sieve_binary *sbin = cgenv->sbin;
	struct cmd_set_context *sctx = (struct cmd_set_context *) ctx->data;
	unsigned int i;	

	sieve_operation_emit_code(sbin, &cmd_set_operation); 

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
		return FALSE;	
		
	/* Generate modifiers (already sorted during validation) */
	sieve_binary_emit_byte(sbin, array_count(&sctx->modifiers));
	for ( i = 0; i < array_count(&sctx->modifiers); i++ ) {
		const struct sieve_variables_modifier * const * modf =
			array_idx(&sctx->modifiers, i);
			
		ext_variables_opr_modifier_emit(sbin, *modf);
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
	if ( !sieve_opr_string_dump(denv, address, "variable") ||
		!sieve_opr_string_dump(denv, address, "value") )
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
 
static int cmd_set_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	struct sieve_variable_storage *storage;
	unsigned int var_index, mdfs, i;
	string_t *value;
	int ret = SIEVE_EXEC_OK;

	/*
	 * Read the normal operands
	 */
		
	/* Read the variable */
	if ( !sieve_variable_operand_read
		(renv, address, &storage, &var_index) ) {
		sieve_runtime_trace_error(renv, "invalid variable operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
		
	/* Read the raw string value */
	if ( !sieve_opr_string_read(renv, address, &value) ) {
		sieve_runtime_trace_error(renv, "invalid string operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
		
	/* Read the number of modifiers used */
	if ( !sieve_binary_read_byte(renv->sbin, address, &mdfs) ) {
		sieve_runtime_trace_error(renv, "invalid modifier count");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/* 
	 * Determine and assign the value 
	 */

	sieve_runtime_trace(renv, "SET action");

	/* Hold value within limits */
	if ( str_len(value) > SIEVE_VARIABLES_MAX_VARIABLE_SIZE )
		str_truncate(value, SIEVE_VARIABLES_MAX_VARIABLE_SIZE);

	T_BEGIN {
		/* Apply modifiers if necessary (sorted during code generation already) */
		if ( str_len(value) > 0 ) {
			for ( i = 0; i < mdfs; i++ ) {
				string_t *new_value;
				const struct sieve_variables_modifier *modf =
					ext_variables_opr_modifier_read(renv, address);

				if ( modf == NULL ) {
					value = NULL;

					sieve_runtime_trace_error(renv, "invalid modifier operand");
					ret = SIEVE_EXEC_BIN_CORRUPT;
					break;
				}
				
				if ( modf->modify != NULL ) {
					if ( !modf->modify(value, &new_value) ) {
						value = NULL;
						ret = SIEVE_EXEC_FAILURE;
						break;
					}

					value = new_value;
					if ( value == NULL )
						break;

					/* Hold value within limits */
					if ( str_len(value) > SIEVE_VARIABLES_MAX_VARIABLE_SIZE )
						str_truncate(value, SIEVE_VARIABLES_MAX_VARIABLE_SIZE);
				}
			}
		}	
		
		/* Actually assign the value if all is well */
		if ( value != NULL ) {
			if ( !sieve_variable_assign(storage, var_index, value) )
				ret = SIEVE_EXEC_BIN_CORRUPT;
		}	
	} T_END;
			
	if ( ret <= 0 ) 
		return ret;		

	return ( value != NULL );
}






