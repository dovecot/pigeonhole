/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include <stdio.h>

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

#include "ext-enotify-common.h"

/* 
 * String test 
 *
 * Syntax:
 *   notify_method_capability [COMPARATOR] [MATCH-TYPE]
 *     <notification-uri: string>
 *     <notification-capability: string>
 *     <key-list: string-list>
 */

static bool tst_notifymc_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_notifymc_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_notifymc_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command notify_method_capability_test = { 
	"notify_method_capability", 
	SCT_TEST, 
	3, 0, FALSE, FALSE,
	tst_notifymc_registered, 
	NULL,
	tst_notifymc_validate, 
	tst_notifymc_generate, 
	NULL 
};

/* 
 * String operation
 */

static bool tst_notifymc_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_notifymc_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation notify_method_capability_operation = { 
	"NOTIFY_METHOD_CAPABILITY",
	&enotify_extension, 
	EXT_ENOTIFY_OPERATION_NOTIFY_METHOD_CAPABILITY, 
	tst_notifymc_operation_dump, 
	tst_notifymc_operation_execute 
};

/* 
 * Optional arguments 
 */

enum tst_notifymc_optional {	
	OPT_END,
	OPT_COMPARATOR,
	OPT_MATCH_TYPE
};

/* 
 * Test registration 
 */

static bool tst_notifymc_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, OPT_COMPARATOR);
	sieve_match_types_link_tags(validator, cmd_reg, OPT_MATCH_TYPE);

	return TRUE;
}

/* 
 * Test validation 
 */

static bool tst_notifymc_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst) 
{ 		
	struct sieve_ast_argument *arg = tst->first_positional;
	
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "notification-uri", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(validator, tst, arg, FALSE) )
		return FALSE;
	
	arg = sieve_ast_argument_next(arg);

	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "notification-capability", 2, SAAT_STRING) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(validator, tst, arg, FALSE) )
		return FALSE;
		
	arg = sieve_ast_argument_next(arg);

	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "key-list", 3, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(validator, tst, arg, FALSE) )
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(validator, tst, arg, &is_match_type, &i_ascii_casemap_comparator);
}

/* 
 * Test generation 
 */

static bool tst_notifymc_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	sieve_operation_emit_code(cgenv->sbin, &notify_method_capability_operation);

 	/* Generate arguments */
	return sieve_generate_arguments(cgenv, ctx, NULL);
}

/* 
 * Code dump 
 */

static bool tst_notifymc_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "NOTIFY_METHOD_CAPABILITY");
	sieve_code_descend(denv);

	/* Handle any optional arguments */
	if ( !sieve_match_dump_optional_operands(denv, address, &opt_code) )
		return FALSE;

	if ( opt_code != SIEVE_MATCH_OPT_END )
		return FALSE;
		
	return
		sieve_opr_string_dump(denv, address, "notify uri") &&
		sieve_opr_string_dump(denv, address, "notify capability") &&
		sieve_opr_stringlist_dump(denv, address, "key list");
}

/* 
 * Code execution 
 */

static int tst_notifymc_operation_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	int ret, mret;
	bool result = TRUE;
	int opt_code = 0;
	const struct sieve_comparator *cmp = &i_octet_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	struct sieve_match_context *mctx;
	string_t *notify_uri, *notify_capability;
	struct sieve_coded_stringlist *key_list;
	const char *cap_value;
	bool matched;

	/*
	 * Read operands 
	 */
	
	/* Handle match-type and comparator operands */
	if ( (ret=sieve_match_read_optional_operands
		(renv, address, &opt_code, &cmp, &mtch)) <= 0 )
		return ret;
	
	/* Check whether we neatly finished the list of optional operands */
	if ( opt_code != SIEVE_MATCH_OPT_END) {
		sieve_runtime_trace_error(renv, "invalid optional operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Read notify uri */
	if ( !sieve_opr_string_read(renv, address, &notify_uri) ) {
		sieve_runtime_trace_error(renv, "invalid notify-uri operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/* Read notify capability */
	if ( !sieve_opr_string_read(renv, address, &notify_capability) ) {
		sieve_runtime_trace_error(renv, "invalid notify-uri operand");
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

	sieve_runtime_trace(renv, "NOTIFY_METHOD_CAPABILITY test");

	cap_value = ext_enotify_runtime_get_method_capability
		(renv, 0 /* FIXME */, notify_uri, str_c(notify_capability));

	if ( cap_value != NULL ) {
		mctx = sieve_match_begin(renv->interp, mtch, cmp, NULL, key_list); 	

		if ( (mret=sieve_match_value(mctx, cap_value, strlen(cap_value))) < 0 )
			result = FALSE;
		matched = ( mret > 0 );		

		if ( (mret=sieve_match_end(mctx)) < 0 ) 
			result = FALSE;
		matched = ( mret > 0 ) || matched;		
	} else {
		matched = FALSE;
	}
	
	if ( result ) {
		sieve_interpreter_set_test_result(renv->interp, matched);
		return SIEVE_EXEC_OK;
	}
	
	sieve_runtime_trace_error(renv, "invalid string list item");
	return SIEVE_EXEC_BIN_CORRUPT;
}
