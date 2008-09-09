/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

/* 
 * Exists test
 *  
 * Syntax:
 *    exists <header-names: string-list>
 */

static bool tst_exists_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_exists_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command tst_exists = { 
	"exists", 
	SCT_TEST, 
	1, 0, FALSE, FALSE,
	NULL, 
	NULL,
	tst_exists_validate, 
	tst_exists_generate, 
	NULL 
};

/* 
 * Exists operation 
 */

static bool tst_exists_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_exists_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation tst_exists_operation = { 
	"EXISTS",
	NULL,
	SIEVE_OPERATION_EXISTS,
	tst_exists_operation_dump, 
	tst_exists_operation_execute 
};

/* 
 * Validation 
 */

static bool tst_exists_validate
  (struct sieve_validator *validator, struct sieve_command_context *tst) 
{
	struct sieve_ast_argument *arg = tst->first_positional;
		
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "header names", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(validator, tst, arg, FALSE);
}

/* 
 * Code generation 
 */

static bool tst_exists_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	sieve_operation_emit_code(cgenv->sbin, &tst_exists_operation);

 	/* Generate arguments */
    return sieve_generate_arguments(cgenv, ctx, NULL);
}

/* 
 * Code dump 
 */

static bool tst_exists_operation_dump
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
    sieve_code_dumpf(denv, "EXISTS");
	sieve_code_descend(denv);

	return sieve_opr_stringlist_dump(denv, address, "header names");
}

/* 
 * Code execution 
 */

static int tst_exists_operation_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool result = TRUE;
	struct sieve_coded_stringlist *hdr_list;
	string_t *hdr_item;
	bool matched;
	
	/* Read header-list */
	if ( (hdr_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid header-list operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	sieve_runtime_trace(renv, "EXISTS test");
		
	/* Iterate through all requested headers to match (must find all specified) */
	hdr_item = NULL;
	matched = TRUE;
	while ( matched && (result=sieve_coded_stringlist_next_item(hdr_list, &hdr_item)) 
		&& hdr_item != NULL ) {
		const char *const *headers;
			
		if ( mail_get_headers_utf8
			(renv->msgdata->mail, str_c(hdr_item), &headers) < 0 ||
			headers[0] == NULL ) {	
			matched = FALSE;				 
		}
	}
	
	/* Set test result for subsequent conditional jump */
	if ( result ) {
		sieve_interpreter_set_test_result(renv->interp, matched);
		return SIEVE_EXEC_OK;
	}
	
	sieve_runtime_trace_error(renv, "invalid header-list item");
	return SIEVE_EXEC_BIN_CORRUPT;
}
