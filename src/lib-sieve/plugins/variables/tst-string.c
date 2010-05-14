/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

#include "ext-variables-common.h"

/* 
 * String test 
 *
 * Syntax:
 *   string [COMPARATOR] [MATCH-TYPE]
 *     <source: string-list> <key-list: string-list>
 */

static bool tst_string_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext, 
		struct sieve_command_registration *cmd_reg);
static bool tst_string_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_string_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

const struct sieve_command_def tst_string = { 
	"string", 
	SCT_TEST, 
	2, 0, FALSE, FALSE,
	tst_string_registered, 
	NULL,
	tst_string_validate, 
	tst_string_generate, 
	NULL 
};

/* 
 * String operation
 */

static bool tst_string_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_string_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def tst_string_operation = { 
	"STRING",
	&variables_extension, 
	EXT_VARIABLES_OPERATION_STRING, 
	tst_string_operation_dump, 
	tst_string_operation_execute 
};

/* 
 * Optional arguments 
 */

enum tst_string_optional {	
	OPT_END,
	OPT_COMPARATOR,
	OPT_MATCH_TYPE
};

/* 
 * Test registration 
 */

static bool tst_string_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(valdtr, cmd_reg, OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg, OPT_MATCH_TYPE);

	return TRUE;
}

/* 
 * Test validation 
 */

static bool tst_string_validate
(struct sieve_validator *valdtr, struct sieve_command *tst) 
{ 		
	struct sieve_ast_argument *arg = tst->first_positional;
	const struct sieve_match_type mcht_default = 
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	const struct sieve_comparator cmp_default = 
		SIEVE_COMPARATOR_DEFAULT(i_octet_comparator);
	
	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "source", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;
	
	arg = sieve_ast_argument_next(arg);

	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(valdtr, tst, arg, &mcht_default, &cmp_default);
}

/* 
 * Test generation 
 */

static bool tst_string_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd) 
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &tst_string_operation);

 	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;
	
	return TRUE;
}

/* 
 * Code dump 
 */

static bool tst_string_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "STRING-TEST");
	sieve_code_descend(denv);

	/* Handle any optional arguments */
	if ( !sieve_match_dump_optional_operands(denv, address, &opt_code) )
		return FALSE;

	if ( opt_code != SIEVE_MATCH_OPT_END )
		return FALSE;
		
	return
		sieve_opr_stringlist_dump(denv, address, "source") &&
		sieve_opr_stringlist_dump(denv, address, "key list");
}

/* 
 * Code execution 
 */

static int tst_string_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	int ret, mret;
	bool result = TRUE;
	int opt_code = 0;
	struct sieve_match_type mcht = 
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	struct sieve_comparator cmp = 
		SIEVE_COMPARATOR_DEFAULT(i_octet_comparator);
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *source;
	struct sieve_coded_stringlist *key_list;
	string_t *src_item;
	bool matched;

	/*
	 * Read operands 
	 */
	
	/* Handle match-type and comparator operands */
	if ( (ret=sieve_match_read_optional_operands
		(renv, address, &opt_code, &cmp, &mcht)) <= 0 )
		return ret;
	
	/* Check whether we neatly finished the list of optional operands*/
	if ( opt_code != SIEVE_MATCH_OPT_END) {
		sieve_runtime_trace_error(renv, "invalid optional operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Read source */
	if ( (source=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid source operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid key-list operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, "STRING test");

	mctx = sieve_match_begin(renv->interp, &mcht, &cmp, NULL, key_list); 	

	/* Iterate through all requested strings to match */
	src_item = NULL;
	matched = FALSE;
	while ( result && !matched && 
		(result=sieve_coded_stringlist_next_item(source, &src_item)) 
		&& src_item != NULL ) {
		const char *src = str_len(src_item) > 0 ? str_c(src_item) : NULL;

		if ( (mret=sieve_match_value
			(mctx, src, str_len(src_item))) < 0 ) {
			result = FALSE;
			break;
		}
		
		matched = ( mret > 0 );				
	}

	if ( (mret=sieve_match_end(&mctx)) < 0 ) 
		result = FALSE;
	else
		matched = ( mret > 0 || matched ); 	
	
	if ( result ) {
		sieve_interpreter_set_test_result(renv->interp, matched);
		return SIEVE_EXEC_OK;
	}
	
	sieve_runtime_trace_error(renv, "invalid string list item");
	return SIEVE_EXEC_BIN_CORRUPT;
}
