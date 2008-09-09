/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */
 
#include "lib.h"

#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

#include "ext-imapflags-common.h"

/*
 * Hasflag test
 *
 * Syntax: 
 *   hasflag [MATCH-TYPE] [COMPARATOR] [<variable-list: string-list>]
 *       <list-of-flags: string-list>
 */

static bool tst_hasflag_registered
	(struct sieve_validator *validator,
		struct sieve_command_registration *cmd_reg);
static bool tst_hasflag_validate
	(struct sieve_validator *validator,	struct sieve_command_context *ctx);
static bool tst_hasflag_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);
 
const struct sieve_command tst_hasflag = { 
	"hasflag", 
	SCT_TEST,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE, 
	tst_hasflag_registered, 
	NULL,
	tst_hasflag_validate, 
	tst_hasflag_generate, 
	NULL 
};

/* 
 * Hasflag operation 
 */

static bool tst_hasflag_operation_dump
	(const struct sieve_operation *op,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_hasflag_operation_execute
	(const struct sieve_operation *op,	
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation hasflag_operation = { 
	"HASFLAG",
	&imapflags_extension,
	EXT_IMAPFLAGS_OPERATION_HASFLAG,
	tst_hasflag_operation_dump,
	tst_hasflag_operation_execute
};

/* 
 * Optional arguments 
 */

enum tst_hasflag_optional {	
	OPT_VARIABLES = SIEVE_MATCH_OPT_LAST,
};

/* 
 * Tag registration 
 */

static bool tst_hasflag_registered
(struct sieve_validator *validator, 
	struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(validator, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);

	return TRUE;
}

/* 
 * Validation 
 */

static bool tst_hasflag_validate
(struct sieve_validator *validator,	struct sieve_command_context *tst)
{
	struct sieve_ast_argument *vars = tst->first_positional;
	struct sieve_ast_argument *keys = sieve_ast_argument_next(vars);
		
	if ( !ext_imapflags_command_validate(validator, tst) )
		return FALSE;
	
	if ( keys == NULL ) {
		keys = vars;
		vars = NULL;
	} else {
		vars->arg_id_code = OPT_VARIABLES;
	}
	
	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(validator, tst, keys, &is_match_type, &i_ascii_casemap_comparator);
}

/*
 * Code generation 
 */

static bool tst_hasflag_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
	sieve_operation_emit_code(cgenv->sbin, &hasflag_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/* 
 * Code dump 
 */
 
static bool tst_hasflag_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,	
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "HASFLAG");
	sieve_code_descend(denv);

	/* Handle any optional arguments */
	do {
		if ( !sieve_match_dump_optional_operands(denv, address, &opt_code) )
			return FALSE;

		switch ( opt_code ) {
		case SIEVE_MATCH_OPT_END:
			break;
		case OPT_VARIABLES:
			sieve_opr_stringlist_dump(denv, address, "variables");
			break;
		default:
			return FALSE;
		}
	} while ( opt_code != SIEVE_MATCH_OPT_END );
			
	return 
		sieve_opr_stringlist_dump(denv, address, "list of flags");
}

/*
 * Interpretation
 */
 
static int _flag_key_extract_init
(void **context, string_t *raw_key)
{
	struct ext_imapflags_iter *iter = t_new(struct ext_imapflags_iter, 1);
	
	ext_imapflags_iter_init(iter, raw_key);
	
	*context = iter; 
	
	return TRUE;
}

static int _flag_key_extract
(void *context, const char **key, size_t *size)
{
	struct ext_imapflags_iter *iter = (struct ext_imapflags_iter *) context;
	
	if ( (*key = ext_imapflags_iter_get_flag(iter)) != NULL ) {
		*size = strlen(*key); 
		return TRUE;
	}
	
	return FALSE;
}

static const struct sieve_match_key_extractor _flag_extractor = {
	_flag_key_extract_init,
	_flag_key_extract
};

static int tst_hasflag_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	int ret, mret;
	bool result = TRUE;
	int opt_code = 0;
	const struct sieve_comparator *cmp = &i_ascii_casemap_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *flag_list, *variables_list = NULL;
	struct ext_imapflags_iter iter;
	const char *flag;
	bool matched;
	
	/*
	 * Read operands
	 */

	/* Handle match-type and comparator operands */
	do {
		if ( (ret=sieve_match_read_optional_operands
			(renv, address, &opt_code, &cmp, &mtch)) <= 0 )
			return ret;
	
		/* Check whether we neatly finished the list of optional operands*/
		switch ( opt_code ) { 
		case SIEVE_MATCH_OPT_END:
			break;
		case OPT_VARIABLES:
			if ( (variables_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
					sieve_runtime_trace_error(renv, "invalid variables-list operand");
				return SIEVE_EXEC_BIN_CORRUPT;
			}
			break;
		default:
			sieve_runtime_trace_error(renv, "invalid optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	} while ( opt_code != SIEVE_MATCH_OPT_END );
		
	/* Read flag list */
	if ( (flag_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid flag-list operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, "HASFLAG test");

	matched = FALSE;
	mctx = sieve_match_begin
		(renv->interp, mtch, cmp, &_flag_extractor, flag_list); 	

	matched = FALSE;

	if ( variables_list != NULL ) {
		string_t *var_item = NULL;
		
		/* Iterate through all requested variables to match */
		while ( result && !matched && 
			(result=sieve_coded_stringlist_next_item(variables_list, &var_item)) 
			&& var_item != NULL ) {
		
			ext_imapflags_get_flags_init(&iter, renv, var_item);	
			while ( !matched && (flag=ext_imapflags_iter_get_flag(&iter)) != NULL ) {
				if ( (mret=sieve_match_value(mctx, flag, strlen(flag))) < 0 ) {
					result = FALSE;
					break;
				}

				matched = ( mret > 0 ); 	
			}
		}
	} else {
		ext_imapflags_get_flags_init(&iter, renv, NULL);	
		while ( !matched && (flag=ext_imapflags_iter_get_flag(&iter)) != NULL ) {
			if ( (mret=sieve_match_value(mctx, flag, strlen(flag))) < 0 ) {
				result = FALSE;
				break;
			}

			matched = ( mret > 0 ); 	
		}
	}

	if ( (mret=sieve_match_end(mctx)) < 0 ) {
		result = FALSE;
	} else
		matched = ( mret > 0 || matched ); 	
	
	/* Assign test result */
	if ( result ) {
		sieve_interpreter_set_test_result(renv->interp, matched);
		return SIEVE_EXEC_OK;
	}
	
	sieve_runtime_trace_error(renv, "invalid string list item");
	return SIEVE_EXEC_BIN_CORRUPT;
}


