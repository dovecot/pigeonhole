/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-mailbox-common.h"

/* 
 * Mailboxexists command 
 *
 * Syntax:
 *    mailboxexists <mailbox-names: string-list>
 */

static bool tst_mailboxexists_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_mailboxexists_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command mailboxexists_test = { 
	"mailboxexists", 
	SCT_TEST, 
	1, 0, FALSE, FALSE,
	NULL, NULL,
	tst_mailboxexists_validate, 
	tst_mailboxexists_generate, 
	NULL 
};

/* 
 * Mailboxexists operation
 */

static bool tst_mailboxexists_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_mailboxexists_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation mailboxexists_operation = { 
	"MAILBOXEXISTS",
	&mailbox_extension, 
	0, 
	tst_mailboxexists_operation_dump, 
	tst_mailboxexists_operation_execute 
};

/* 
 * Test validation 
 */

static bool tst_mailboxexists_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst) 
{ 		
	struct sieve_ast_argument *arg = tst->first_positional;
	
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "mailbox-names", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(validator, tst, arg, FALSE);
}

/* 
 * Test generation 
 */

static bool tst_mailboxexists_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *tst) 
{
	sieve_operation_emit_code(cgenv->sbin, &mailboxexists_operation);

 	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

/* 
 * Code dump 
 */

static bool tst_mailboxexists_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "MAILBOXEXISTS");
	sieve_code_descend(denv);
		
	return
		sieve_opr_stringlist_dump(denv, address, "mailbox-names");
}

/* 
 * Code execution 
 */

static int tst_mailboxexists_operation_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_coded_stringlist *mailbox_names;
	string_t *uri_item;
	bool result = TRUE, all_exist = TRUE;

	/*
	 * Read operands 
	 */
	
	/* Read notify uris */
	if ( (mailbox_names=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid mailbox-names operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, "MAILBOXEXISTS command");

	uri_item = NULL;
	while ( (result=sieve_coded_stringlist_next_item(mailbox_names, &uri_item)) 
		&& uri_item != NULL ) {
		
		if ( TRUE ) {
			all_exist = FALSE;
			break;
		}
	}
	
	if ( !result ) {
		sieve_runtime_trace_error(renv, "invalid mailbox name item");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	sieve_interpreter_set_test_result(renv->interp, all_exist);
	return SIEVE_EXEC_OK;
}
