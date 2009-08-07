/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-match-types.h"
#include "sieve-comparators.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"

#include "ext-notify-common.h"
 
/* 
 * Denotify command (NOT IMPLEMENTED)
 *
 * Syntax:
 *   denotify [MATCH-TYPE string] [<":low" / ":normal" / ":high">]
 */

static bool cmd_denotify_registered
	(struct sieve_validator *valdtr, struct sieve_command_registration *cmd_reg);
static bool cmd_denotify_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command cmd_denotify = {
	"denotify",
	SCT_COMMAND,
	0, 0, FALSE, FALSE,
	cmd_denotify_registered,
	NULL,
	NULL, 
	cmd_denotify_generate, 
	NULL
};

/*
 * Tagged arguments
 */

/* Forward declarations */

static bool tag_match_type_is_instance_of
	(struct sieve_validator *validator, struct sieve_command_context *cmd,
		struct sieve_ast_argument *arg);
static bool tag_match_type_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg,
		struct sieve_command_context *cmd);

/* Argument object */

const struct sieve_argument denotify_match_tag = {
	"MATCH-TYPE-STRING",
	tag_match_type_is_instance_of,
	NULL,
	tag_match_type_validate,
	NULL, NULL
};

/* Codes for optional operands */

enum cmd_denotify_optional {
  OPT_END,
  OPT_IMPORTANCE,
  OPT_MATCH_TYPE,
	OPT_MATCH_KEY
};

/* 
 * Denotify operation 
 */

static bool cmd_denotify_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address);

const struct sieve_operation denotify_operation = { 
	"DENOTIFY",
	&notify_extension,
	EXT_NOTIFY_OPERATION_DENOTIFY,
	cmd_denotify_operation_dump,
	NULL
};

/*
 * Tag validation
 */

static bool tag_match_type_is_instance_of
(struct sieve_validator *valdtr, struct sieve_command_context *cmd,
	struct sieve_ast_argument *arg)
{
	return match_type_tag.is_instance_of(valdtr, cmd, arg);
}

static bool tag_match_type_validate
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	if ( !match_type_tag.validate(valdtr, arg, cmd) )
		return FALSE;

	if ( *arg == NULL ) {
		sieve_argument_validate_error(valdtr, tag, 
			"the MATCH-TYPE argument (:%s) for the denotify command requires "
			"an additional key-string paramterer, but no more arguments were found", 
			sieve_ast_argument_tag(tag));
		return FALSE;	
	}
	
	if ( sieve_ast_argument_type(*arg) != SAAT_STRING ) 
	{
		sieve_argument_validate_error(valdtr, *arg, 
			"the MATCH-TYPE argument (:%s) for the denotify command requires "
			"an additional key-string parameter, but %s was found", 
			sieve_ast_argument_tag(tag), sieve_ast_argument_name(*arg));
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, cmd, *arg, FALSE) ) 
		return FALSE;

	if ( !sieve_match_type_validate
		(valdtr, cmd, *arg, &is_match_type, &i_octet_comparator) )
		return FALSE;

	tag->argument = &match_type_tag;

	(*arg)->arg_id_code = OPT_MATCH_KEY;

	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/*
 * Command registration
 */

static bool cmd_denotify_registered
(struct sieve_validator *valdtr, struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag
		(valdtr, cmd_reg, &denotify_match_tag, OPT_MATCH_TYPE);

	ext_notify_register_importance_tags(valdtr, cmd_reg, OPT_IMPORTANCE);

	return TRUE;
}

/*
 * Code generation
 */

static bool cmd_denotify_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
	sieve_operation_emit_code(cgenv->sbin, &denotify_operation);

	/* Emit source line */
	sieve_code_source_line_emit(cgenv->sbin, sieve_command_source_line(ctx));

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, ctx, NULL);
}

/* 
 * Code dump
 */
 
static bool cmd_denotify_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{	
	int opt_code = 1;
	
	sieve_code_dumpf(denv, "%s", op->mnemonic);
	sieve_code_descend(denv);	

	/* Source line */
	if ( !sieve_code_source_line_dump(denv, address) )
		return FALSE;

	/* Dump optional operands */
	if ( sieve_operand_optional_present(denv->sbin, address) ) {
		while ( opt_code != 0 ) {
			sieve_code_mark(denv);
			
			if ( !sieve_operand_optional_read(denv->sbin, address, &opt_code) ) 
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_MATCH_KEY:
				if ( !sieve_opr_string_dump(denv, address, "key-string") )
					return FALSE;
				break;
			case OPT_MATCH_TYPE:
				if ( !sieve_opr_match_type_dump(denv, address) )
					return FALSE;
				break;
			case OPT_IMPORTANCE:
				if ( !sieve_opr_number_dump(denv, address, "importance") )
					return FALSE;
				break;
			default:
				return FALSE;
			}
		}
	}
	
	return TRUE;
}

/* 
 * Code execution
 */

static int cmd_notify_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	struct ext_notify_action *act;
	pool_t pool;
	int opt_code = 1;
	sieve_number_t importance = 1;
	const struct sieve_match_type *match_type = NULL;
	string_t *match_key = NULL; 
	unsigned int source_line;

	/*
	 * Read operands
	 */
		
	/* Source line */
	if ( !sieve_code_source_line_read(renv, address, &source_line) ) {
		sieve_runtime_trace_error(renv, "invalid source line");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/* Optional operands */	
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) ) {
				sieve_runtime_trace_error(renv, "invalid optional operand");
				return SIEVE_EXEC_BIN_CORRUPT;
			}

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_MATCH_TYPE:
				if ( (match_type = sieve_opr_match_type_read(renv, address)) == NULL ) {
					sieve_runtime_trace_error(renv, "invalid match type operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
				break;
			case OPT_MATCH_KEY:
				if ( !sieve_opr_string_read(renv, address, &match_key) ) {
					sieve_runtime_trace_error(renv, "invalid from operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
				break;
			case OPT_IMPORTANCE:
				if ( !sieve_opr_number_read(renv, address, &importance) ) {
					sieve_runtime_trace_error(renv, "invalid importance operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
	
				/* Enforce 0 < importance < 4 (just to be sure) */
				if ( importance < 1 ) 
					importance = 1;
				else if ( importance > 3 )
					importance = 3;
				break;
			default:
				sieve_runtime_trace_error(renv, "unknown optional operand: %d", 
					opt_code);
				return SIEVE_EXEC_BIN_CORRUPT;
			}
		}
	}
		
	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, "DENOTIFY action");	

	return SIEVE_EXEC_OK;
}



