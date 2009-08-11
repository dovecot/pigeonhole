/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "testsuite-common.h"
#include "testsuite-mailstore.h"

/*
 * Test_mailbox command
 *
 * Syntax:   
 *   test_mailbox ( :create / :delete ) <mailbox: string>
 */

static bool cmd_test_mailbox_registered
	(struct sieve_validator *valdtr, 
		struct sieve_command_registration *cmd_reg);
static bool cmd_test_mailbox_validate
	(struct sieve_validator *valdtr, struct sieve_command_context *cmd);
static bool cmd_test_mailbox_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command cmd_test_mailbox = { 
	"test_mailbox", 
	SCT_COMMAND, 
	1, 0, FALSE, FALSE,
	cmd_test_mailbox_registered, 
	NULL,
	cmd_test_mailbox_validate, 
	cmd_test_mailbox_generate, 
	NULL 
};

/* 
 * Operations
 */ 

static bool cmd_test_mailbox_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_test_mailbox_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);
 
/* Test_mailbox_create operation */

const struct sieve_operation test_mailbox_create_operation = { 
	"TEST_MAILBOX_CREATE",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_MAILBOX_CREATE,
	cmd_test_mailbox_operation_dump, 
	cmd_test_mailbox_operation_execute 
};

/* Test_mailbox_delete operation */

const struct sieve_operation test_mailbox_delete_operation = { 
	"TEST_MAILBOX_DELETE",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_MAILBOX_DELETE,
	cmd_test_mailbox_operation_dump, 
	cmd_test_mailbox_operation_execute 
};

/*
 * Compiler context data
 */
 
enum test_mailbox_operation {
	MAILBOX_OP_CREATE, 
	MAILBOX_OP_DELETE,
	MAILBOX_OP_LAST
};

const struct sieve_operation *test_mailbox_operations[] = {
	&test_mailbox_create_operation,
	&test_mailbox_delete_operation
};

struct cmd_test_mailbox_context_data {
	enum test_mailbox_operation mailbox_op;
	const char *folder;
};

/* 
 * Command tags 
 */
 
static bool cmd_test_mailbox_validate_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);

static const struct sieve_argument test_mailbox_create_tag = { 
	"create", 
	NULL, NULL,
	cmd_test_mailbox_validate_tag,
	NULL, NULL 
};

static const struct sieve_argument test_mailbox_delete_tag = { 
	"delete", 
	NULL, NULL, 
	cmd_test_mailbox_validate_tag,
	NULL, NULL 
};

static bool cmd_test_mailbox_registered
(struct sieve_validator *valdtr, struct sieve_command_registration *cmd_reg) 
{
	/* Register our tags */
	sieve_validator_register_tag(valdtr, cmd_reg, &test_mailbox_create_tag, 0); 	
	sieve_validator_register_tag(valdtr, cmd_reg, &test_mailbox_delete_tag, 0); 	

	return TRUE;
}

static bool cmd_test_mailbox_validate_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct cmd_test_mailbox_context_data *ctx_data = 
		(struct cmd_test_mailbox_context_data *) cmd->data;	
	
	if ( ctx_data != NULL ) {
		sieve_argument_validate_error
			(valdtr, *arg, "exactly one of the ':create' or ':delete' tags must be "
				"specified for the test_mailbox command, but more were found");
		return NULL;		
	}
	
	ctx_data = p_new
		(sieve_command_pool(cmd), struct cmd_test_mailbox_context_data, 1);
	cmd->data = ctx_data;
	
	if ( (*arg)->argument == &test_mailbox_create_tag ) 
		ctx_data->mailbox_op = MAILBOX_OP_CREATE;
	else
		ctx_data->mailbox_op = MAILBOX_OP_DELETE;

	/* Delete this tag */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	return TRUE;
}

/* 
 * Validation 
 */

static bool cmd_test_mailbox_validate
(struct sieve_validator *valdtr, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	
	if ( cmd->data == NULL ) {
		sieve_command_validate_error(valdtr, cmd, 
			"the test_mailbox command requires either the :create or the :delete tag "
			"to be specified");
		return FALSE;		
	}
		
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "mailbox", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/* 
 * Code generation 
 */

static bool cmd_test_mailbox_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *cmd)
{
	struct cmd_test_mailbox_context_data *ctx_data =
		(struct cmd_test_mailbox_context_data *) cmd->data; 

	i_assert( ctx_data->mailbox_op < MAILBOX_OP_LAST );
	
	/* Emit operation */
	sieve_operation_emit_code(cgenv->sbin, 
		test_mailbox_operations[ctx_data->mailbox_op]);
	  	
 	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_test_mailbox_operation_dump
(const struct sieve_operation *op,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "%s:", op->mnemonic);
	
	sieve_code_descend(denv);
	
	return sieve_opr_string_dump(denv, address, "mailbox");
}


/*
 * Intepretation
 */
 
static int cmd_test_mailbox_operation_execute
(const struct sieve_operation *op,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *mailbox = NULL;

	/* 
	 * Read operands 
	 */

	/* Index */

	if ( !sieve_opr_string_read(renv, address, &mailbox) ) {
		sieve_runtime_trace_error(renv, "invalid mailbox operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/*
	 * Perform operation
	 */
		
	sieve_runtime_trace(renv, "%s %s:", op->mnemonic, str_c(mailbox));

	if ( op == &test_mailbox_create_operation )
		testsuite_mailstore_mailbox_create(renv, str_c(mailbox));

	return SIEVE_EXEC_OK;
}
