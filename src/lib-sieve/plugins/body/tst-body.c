/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */
 
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

#include "ext-body-common.h"

/*
 * Types
 */

enum tst_body_transform {
	TST_BODY_TRANSFORM_RAW,
	TST_BODY_TRANSFORM_CONTENT,
	TST_BODY_TRANSFORM_TEXT
};

/* 
 * Body test 
 *
 * Syntax
 *   body [COMPARATOR] [MATCH-TYPE] [BODY-TRANSFORM]
 *     <key-list: string-list>
 */

static bool tst_body_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_body_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_body_generate
	(const struct sieve_codegen_env *cgenv,	struct sieve_command_context *ctx);

const struct sieve_command body_test = { 
	"body", 
	SCT_TEST, 
	1, 0, FALSE, FALSE,
	tst_body_registered, 
	NULL,
	tst_body_validate, 
	tst_body_generate, 
	NULL 
};

/* 
 * Body operation 
 */

static bool ext_body_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int ext_body_operation_execute
	(const struct sieve_operation *op,
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation body_operation = { 
	"body",
	&body_extension,
	0,
	ext_body_operation_dump, 
	ext_body_operation_execute 
};

/*
 * Optional operands
 */

enum tst_body_optional {	
	OPT_BODY_TRANSFORM = SIEVE_MATCH_OPT_LAST
};

/* 
 * Tagged arguments 
 */

/* Forward declarations */

static bool tag_body_transform_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);
static bool tag_body_transform_generate	
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command_context *cmd);

/* Argument objects */
 
static const struct sieve_argument body_raw_tag = { 
	"raw", 
	NULL, NULL,
	tag_body_transform_validate, 
	NULL, 
	tag_body_transform_generate 
};

static const struct sieve_argument body_content_tag = { 
	"content", 
	NULL, NULL,
	tag_body_transform_validate, 
	NULL, 
	tag_body_transform_generate 
};

static const struct sieve_argument body_text_tag = { 
	"text", 
	NULL, NULL,
	tag_body_transform_validate, 
	NULL, 
	tag_body_transform_generate
};

/* Argument implementation */
 
static bool tag_body_transform_validate
(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	enum tst_body_transform transform;
	struct sieve_ast_argument *tag = *arg;

	/* BODY-TRANSFORM:
	 *   :raw
	 *     / :content <content-types: string-list>
	 *     / :text
	 */
	if ( (bool) cmd->data ) {
		sieve_command_validate_error(validator, cmd, 
			"the :raw, :content and :text arguments for the body test are mutually "
			"exclusive, but more than one was specified");
		return FALSE;
	}

	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);

	/* :content tag has a string-list argument */
	if ( tag->argument == &body_raw_tag ) 
		transform = TST_BODY_TRANSFORM_RAW;
		
	else if ( tag->argument == &body_text_tag )
		transform = TST_BODY_TRANSFORM_TEXT;
		
	else if ( tag->argument == &body_content_tag ) {
		/* Check syntax:
		 *   :content <content-types: string-list>
		 */
		if ( !sieve_validate_tag_parameter
			(validator, cmd, tag, *arg, SAAT_STRING_LIST) ) {
			return FALSE;
		}
		
		if ( !sieve_validator_argument_activate(validator, cmd, *arg, FALSE) )
			return FALSE;
		
		/* Assign tag parameters */
		tag->parameters = *arg;
		*arg = sieve_ast_arguments_detach(*arg,1);
		
		transform = TST_BODY_TRANSFORM_CONTENT;
	} else 
		return FALSE;
	
	/* Signal the presence of this tag */
	cmd->data = (void *) TRUE;
		
	/* Assign context data */
	tag->context = (void *) transform;	
		
	return TRUE;
}

/* 
 * Command Registration 
 */

static bool tst_body_registered
(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(validator, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);
	
	sieve_validator_register_tag
		(validator, cmd_reg, &body_raw_tag, OPT_BODY_TRANSFORM); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &body_content_tag, OPT_BODY_TRANSFORM); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &body_text_tag, OPT_BODY_TRANSFORM); 	
	
	return TRUE;
}

/* 
 * Validation 
 */
 
static bool tst_body_validate
(struct sieve_validator *validator, struct sieve_command_context *tst) 
{ 		
	struct sieve_ast_argument *arg = tst->first_positional;
					
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "key list", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(validator, tst, arg, FALSE) )
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(validator, tst, arg, &is_match_type, &i_ascii_casemap_comparator);
}

/*
 * Code generation
 */
 
static bool tst_body_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	(void)sieve_operation_emit_code(cgenv->sbin, &body_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, ctx, NULL);
}

static bool tag_body_transform_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	enum tst_body_transform transform =	(enum tst_body_transform) arg->context;
	
	sieve_binary_emit_byte(cgenv->sbin, transform);
	sieve_generate_argument_parameters(cgenv, cmd, arg); 
			
	return TRUE;
}

/* 
 * Code dump 
 */
 
static bool ext_body_operation_dump
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	enum tst_body_transform transform;
	int opt_code = 0;

	sieve_code_dumpf(denv, "BODY");
	sieve_code_descend(denv);

	/* Handle any optional arguments */
	do {
		
		if ( !sieve_match_dump_optional_operands(denv, address, &opt_code) )
			return FALSE;

		switch ( opt_code ) {
		case SIEVE_MATCH_OPT_END:
			break;
		case OPT_BODY_TRANSFORM:
			if ( !sieve_binary_read_byte(denv->sbin, address, &transform) )
				return FALSE;
			
			switch ( transform ) {
			case TST_BODY_TRANSFORM_RAW:
				sieve_code_dumpf(denv, "BODY-TRANSFORM: RAW");
				break;
			case TST_BODY_TRANSFORM_TEXT:
				sieve_code_dumpf(denv, "BODY-TRANSFORM: TEXT");
				break;
			case TST_BODY_TRANSFORM_CONTENT:
				sieve_code_dumpf(denv, "BODY-TRANSFORM: CONTENT");
				
				sieve_code_descend(denv);
				if ( !sieve_opr_stringlist_dump(denv, address, "content types") )
					return FALSE;
				sieve_code_ascend(denv);
				break;
			default:
				return FALSE;
			}
			break;
		default: 
			return FALSE;
		}
	} while ( opt_code != SIEVE_MATCH_OPT_END );

	return sieve_opr_stringlist_dump(denv, address, "key list");
}

/*
 * Interpretation
 */

static int ext_body_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	static const char * const _no_content_types[] = { "", NULL };
	
	int ret = SIEVE_EXEC_OK;
	int opt_code = 0;
	int mret;
	const struct sieve_comparator *cmp = &i_ascii_casemap_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	enum tst_body_transform transform;
	struct sieve_coded_stringlist *key_list, *ctype_list = NULL;
	struct sieve_match_context *mctx;
	const char * const *content_types = _no_content_types;
	struct ext_body_part *body_parts;
	bool mvalues_active;
	bool matched;

	/*
	 * Read operands
	 */
	
	/* Handle any optional operands */
	do {
		if ( (ret=sieve_match_read_optional_operands
			(renv, address, &opt_code, &cmp, &mtch)) <= 0 )
			return ret;
			
		switch ( opt_code ) {
		case SIEVE_MATCH_OPT_END: 
			break;
		case OPT_BODY_TRANSFORM:
			if ( !sieve_binary_read_byte(renv->sbin, address, &transform) ||
				transform > TST_BODY_TRANSFORM_TEXT ) {
				sieve_runtime_trace_error(renv, "invalid body transform type");
				return SIEVE_EXEC_BIN_CORRUPT;
			}
			
			if ( transform == TST_BODY_TRANSFORM_CONTENT ) {				
				if ( (ctype_list=sieve_opr_stringlist_read(renv, address)) 
					== NULL ) {
					sieve_runtime_trace_error(renv, 
						"invalid :content body transform operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
			}
			break;

		default:
			sieve_runtime_trace_error(renv, "unknown optional operand");
			return FALSE;
		}
	} while ( opt_code != SIEVE_MATCH_OPT_END );
		
	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid key-list operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	if ( ctype_list != NULL && !sieve_coded_stringlist_read_all
		(ctype_list, pool_datastack_create(), &content_types) ) {
		sieve_runtime_trace_error(renv, "invalid content-type-list operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, "BODY action");
	
	/* Extract requested parts */
	
	if ( !ext_body_get_content
		(renv, content_types, transform != TST_BODY_TRANSFORM_RAW, &body_parts) ) {
		return SIEVE_EXEC_FAILURE;
	}

	/* Disable match values processing as required by RFC */
		
	mvalues_active = sieve_match_values_set_enabled(renv->interp, FALSE);

	/* Iterate through all requested body parts to match */

	matched = FALSE;	
	mctx = sieve_match_begin(renv->interp, mtch, cmp, NULL, key_list); 	
	while ( !matched && body_parts->content != NULL ) {
		if ( (mret=sieve_match_value(mctx, body_parts->content, body_parts->size)) 	
			< 0) 
		{
			sieve_runtime_trace_error(renv, "invalid string list item");
			ret = SIEVE_EXEC_BIN_CORRUPT;
			break;
		}
		
		matched = ( mret > 0 );			
		body_parts++;	
	}

	if ( (mret=sieve_match_end(mctx)) < 0 ) {
		sieve_runtime_trace_error(renv, "invalid string list item");
		ret = SIEVE_EXEC_BIN_CORRUPT;
	} else	
		matched = ( mret > 0 || matched ); 	
	
	/* Restore match values processing */ 
	
	(void)sieve_match_values_set_enabled(renv->interp, mvalues_active);
	
	/* Set test result */	
	
	if ( ret == SIEVE_EXEC_OK )
		sieve_interpreter_set_test_result(renv->interp, matched);

	return ret;
}
