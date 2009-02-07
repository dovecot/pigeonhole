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
#include "testsuite-smtp.h"

/*
 * Test_message command
 *
 * Syntax:   
 *   test_message ( :smtp / :mailbox <mailbox: string> ) <index: number>
 */

static bool cmd_test_message_registered
	(struct sieve_validator *valdtr, 
		struct sieve_command_registration *cmd_reg);
static bool cmd_test_message_validate
	(struct sieve_validator *valdtr, struct sieve_command_context *cmd);
static bool cmd_test_message_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command cmd_test_message = { 
	"test_message", 
	SCT_COMMAND, 
	1, 0, FALSE, FALSE,
	cmd_test_message_registered, 
	NULL,
	cmd_test_message_validate, 
	cmd_test_message_generate, 
	NULL 
};

/* 
 * Operations
 */ 
 
/* Test_message_smtp operation */

static bool cmd_test_message_smtp_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_test_message_smtp_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation test_message_smtp_operation = { 
	"TEST_MESSAGE_SMTP",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_MESSAGE_SMTP,
	cmd_test_message_smtp_operation_dump, 
	cmd_test_message_smtp_operation_execute 
};

/* Test_message_mailbox operation */

static bool cmd_test_message_mailbox_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_test_message_mailbox_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation test_message_mailbox_operation = { 
	"TEST_MESSAGE_MAILBOX",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_MESSAGE_MAILBOX,
	cmd_test_message_mailbox_operation_dump, 
	cmd_test_message_mailbox_operation_execute 
};

/*
 * Compiler context data
 */
 
enum test_message_source {
	MSG_SOURCE_SMTP, 
	MSG_SOURCE_MAILBOX,
	MSG_SOURCE_LAST
};

const struct sieve_operation *test_message_operations[] = {
	&test_message_smtp_operation,
	&test_message_mailbox_operation
};

struct cmd_test_message_context_data {
	enum test_message_source msg_source;
	const char *folder;
};

#define CMD_TEST_MESSAGE_ERROR_DUP_TAG \
	"exactly one of the ':smtp' or ':folder' tags must be specified " \
	"for the test_message command, but more were found"

/* 
 * Command tags 
 */
 
static bool cmd_test_message_validate_smtp_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);
static bool cmd_test_message_validate_folder_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);

static const struct sieve_argument test_message_smtp_tag = { 
	"smtp", 
	NULL, NULL,
	cmd_test_message_validate_smtp_tag,
	NULL, NULL 
};

static const struct sieve_argument test_message_folder_tag = { 
	"folder", 
	NULL, NULL, 
	cmd_test_message_validate_folder_tag,
	NULL, NULL 
};

static bool cmd_test_message_registered
(struct sieve_validator *valdtr, struct sieve_command_registration *cmd_reg) 
{
	/* Register our tags */
	sieve_validator_register_tag(valdtr, cmd_reg, &test_message_folder_tag, 0); 	
	sieve_validator_register_tag(valdtr, cmd_reg, &test_message_smtp_tag, 0); 	

	return TRUE;
}

static struct cmd_test_message_context_data *cmd_test_message_validate_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct cmd_test_message_context_data *ctx_data = 
		(struct cmd_test_message_context_data *) cmd->data;	
	
	if ( ctx_data != NULL ) {
		sieve_argument_validate_error
			(valdtr, *arg, CMD_TEST_MESSAGE_ERROR_DUP_TAG);
		return NULL;		
	}
	
	ctx_data = p_new
		(sieve_command_pool(cmd), struct cmd_test_message_context_data, 1);
	cmd->data = ctx_data;
	
	/* Delete this tag */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	return ctx_data;
}

static bool cmd_test_message_validate_smtp_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct cmd_test_message_context_data *ctx_data = 
		cmd_test_message_validate_tag(valdtr, arg, cmd);

	/* Return value is NULL on error */
	if ( ctx_data == NULL ) return FALSE;
	
	/* Assign chosen message source */
	ctx_data->msg_source = MSG_SOURCE_SMTP;
			
	return TRUE;
}

static bool cmd_test_message_validate_folder_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct cmd_test_message_context_data *ctx_data = 
		cmd_test_message_validate_tag(valdtr, arg, cmd);
	
	/* Return value is NULL on error */
	if ( ctx_data == NULL ) return FALSE;

	/* Assign chose message source */
	ctx_data->msg_source = MSG_SOURCE_MAILBOX;
	
	/* Check syntax:
	 *   :folder string
	 */
	if ( !sieve_validate_tag_parameter
		(valdtr, cmd, tag, *arg, SAAT_STRING) ) {
		return FALSE;
	}
			
	return TRUE;
}

/* 
 * Validation 
 */

static bool cmd_test_message_validate
(struct sieve_validator *valdtr, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	
	if ( cmd->data == NULL ) {
		sieve_command_validate_error(valdtr, cmd, 
			"the test_message command requires either the :smtp or the :mailbox tag "
			"to be specified");
		return FALSE;		
	}
		
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "index", 1, SAAT_NUMBER) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/* 
 * Code generation 
 */

static bool cmd_test_message_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *cmd)
{
	struct cmd_test_message_context_data *ctx_data =
		(struct cmd_test_message_context_data *) cmd->data; 

	i_assert( ctx_data->msg_source < MSG_SOURCE_LAST );
	
	/* Emit operation */
	sieve_operation_emit_code(cgenv->sbin, 
		test_message_operations[ctx_data->msg_source]);
	
 	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;
	  
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_test_message_smtp_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "TEST_MESSAGE_SMTP:");
	
	sieve_code_descend(denv);
	
	return sieve_opr_number_dump(denv, address, "index");
}

static bool cmd_test_message_mailbox_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "TEST_MESSAGE_MAILBOX:");
	
	sieve_code_descend(denv);

	return 
		sieve_opr_string_dump(denv, address, "folder") &&
		sieve_opr_number_dump(denv, address, "index");
}

/*
 * Intepretation
 */
 
static int cmd_test_message_smtp_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	sieve_number_t msg_index;

	/* Index */
	if ( !sieve_opr_number_read(renv, address, &msg_index) ) {
		sieve_runtime_trace_error(renv, "invalid index operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
		
	sieve_runtime_trace(renv, "TEST_MESSAGE_SMTP [%d]", msg_index);

	return testsuite_smtp_get(renv, msg_index);
}

static int cmd_test_message_mailbox_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *folder;
	sieve_number_t msg_index;

	/* Folder */
	if ( !sieve_opr_string_read(renv, address, &folder) ) {
		sieve_runtime_trace_error(renv, "invalid folder operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/* Index */
	if ( !sieve_opr_number_read(renv, address, &msg_index) ) {
		sieve_runtime_trace_error(renv, "invalid index operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
		
	sieve_runtime_trace(renv, "TEST_MESSAGE_MAILBOX \"%s\" [%d]", 
		str_c(folder), msg_index);

	/* FIXME: to be implemented */

	return SIEVE_EXEC_OK;
}





