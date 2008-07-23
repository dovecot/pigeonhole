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

#include "ext-body-common.h"

/* 
 * Forward declarations 
 */

static bool ext_body_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool ext_body_operation_execute
	(const struct sieve_operation *op,
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool tst_body_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_body_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_body_generate
	(const struct sieve_codegen_env *cgenv,	struct sieve_command_context *ctx);

/* body test 
 *
 * Syntax
 *   body [COMPARATOR] [MATCH-TYPE] [BODY-TRANSFORM]
 *     <key-list: string-list>
 */
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

/* Body operation */

const struct sieve_operation body_operation = { 
	"body",
	&body_extension,
	0,
	ext_body_operation_dump, 
	ext_body_operation_execute 
};

enum tst_body_optional {	
	OPT_END,
	OPT_COMPARATOR,
	OPT_MATCH_TYPE,
	OPT_BODY_TRANSFORM
};

enum tst_body_transform {
	TST_BODY_TRANSFORM_RAW,
	TST_BODY_TRANSFORM_CONTENT,
	TST_BODY_TRANSFORM_TEXT
};

/* 
 * Custom command tags 
 */

static bool tag_body_transform_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);
static bool tag_body_transform_generate	
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command_context *cmd);
 
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
	sieve_comparators_link_tag(validator, cmd_reg, OPT_COMPARATOR);
	sieve_match_types_link_tags(validator, cmd_reg, OPT_MATCH_TYPE);
	
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
	return sieve_match_type_validate(validator, tst, arg);
}

/*
 * Generation
 */
 
static bool tst_body_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	(void)sieve_operation_emit_code(cgenv->sbin, &body_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
		return FALSE;

	return TRUE;
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
	int opt_code = 1;
	enum tst_body_transform transform;

	sieve_code_dumpf(denv, "BODY");
	sieve_code_descend(denv);

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(denv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(denv->sbin, address, &opt_code) ) 
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_COMPARATOR:
				sieve_opr_comparator_dump(denv, address);
				break;
			case OPT_MATCH_TYPE:
				sieve_opr_match_type_dump(denv, address);
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
					if ( !sieve_opr_stringlist_dump(denv, address) )
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
 		}
	}

	return
		sieve_opr_stringlist_dump(denv, address);
}

static bool ext_body_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool result = TRUE;
	int opt_code = 1;
	const struct sieve_comparator *cmp = &i_octet_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	enum tst_body_transform transform;
	struct sieve_coded_stringlist *key_list, *ctype_list = NULL;
	struct sieve_match_context *mctx;
	const char * const *content_types = { NULL };
	struct ext_body_part *body_parts;
	bool matched;
	
	sieve_runtime_trace(renv, "BODY action");

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) )
				return FALSE;

			switch ( opt_code ) {
			case 0: 
				break;
			case OPT_COMPARATOR:
				cmp = sieve_opr_comparator_read(renv, address);
				break;
			case OPT_MATCH_TYPE:
				mtch = sieve_opr_match_type_read(renv, address);
				break;
			case OPT_BODY_TRANSFORM:
				if ( !sieve_binary_read_byte(renv->sbin, address, &transform) ||
					transform > TST_BODY_TRANSFORM_TEXT )
					return FALSE;
				
				if ( transform == TST_BODY_TRANSFORM_CONTENT ) {				
					if ( (ctype_list=sieve_opr_stringlist_read(renv, address)) 
						== NULL )
						return FALSE;
				}
				break;

			default:
				return FALSE;
			}
		}
	}

	t_push();
		
	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		t_pop();
		return FALSE;
	}
	
	if ( ctype_list != NULL && !sieve_coded_stringlist_read_all
    (ctype_list, pool_datastack_create(), &content_types) ) {
  	t_pop();
  	return FALSE;
  }

	if ( !ext_body_get_content
		(renv, content_types, transform == TST_BODY_TRANSFORM_RAW, &body_parts) ) {
		t_pop();
		return FALSE;
	}

	mctx = sieve_match_begin(renv->interp, mtch, cmp, key_list); 	

	/* Iterate through all requested body parts to match */
	matched = FALSE;
	while ( !matched && body_parts->content != NULL ) {
		if ( sieve_match_value(mctx, body_parts->content, body_parts->size) )
			matched = TRUE;			
		body_parts++;	
	}

	matched = sieve_match_end(mctx) || matched; 	
	
	t_pop();
	
	if ( result )
		sieve_interpreter_set_test_result(renv->interp, matched);
	
	return result;
}
