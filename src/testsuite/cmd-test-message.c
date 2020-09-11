/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"
#include "istream.h"
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-message.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "testsuite-common.h"
#include "testsuite-smtp.h"
#include "testsuite-mailstore.h"

/*
 * Commands
 */

/* Test_message command
 *
 * Syntax:
 *   test_message ( :smtp / :mailbox <mailbox: string> ) <index: number>
 */

static bool
cmd_test_message_registered(struct sieve_validator *valdtr,
			    const struct sieve_extension *ext,
			    struct sieve_command_registration *cmd_reg);
static bool
cmd_test_message_validate(struct sieve_validator *valdtr,
			  struct sieve_command *cmd);
static bool
cmd_test_message_generate(const struct sieve_codegen_env *cgenv,
			  struct sieve_command *ctx);

const struct sieve_command_def cmd_test_message = {
	.identifier = "test_message",
	.type = SCT_HYBRID,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = cmd_test_message_registered,
	.validate = cmd_test_message_validate,
	.generate = cmd_test_message_generate,
};

/* Test_message_print command
 *
 * Syntax:
 *   test_message_print
 */

static bool
cmd_test_message_print_generate(const struct sieve_codegen_env *cgenv,
				struct sieve_command *cmd);

const struct sieve_command_def cmd_test_message_print = {
	.identifier = "test_message_print",
	.type = SCT_COMMAND,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.generate = cmd_test_message_print_generate,
};

/*
 * Operations
 */

/* Test_message_smtp operation */

static bool
cmd_test_message_smtp_operation_dump(const struct sieve_dumptime_env *denv,
				     sieve_size_t *address);
static int
cmd_test_message_smtp_operation_execute(const struct sieve_runtime_env *renv,
					sieve_size_t *address);

const struct sieve_operation_def test_message_smtp_operation = {
	.mnemonic = "TEST_MESSAGE_SMTP",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_MESSAGE_SMTP,
	.dump = cmd_test_message_smtp_operation_dump,
	.execute = cmd_test_message_smtp_operation_execute,
};

/* Test_message_mailbox operation */

static bool
cmd_test_message_mailbox_operation_dump(const struct sieve_dumptime_env *denv,
					sieve_size_t *address);
static int
cmd_test_message_mailbox_operation_execute(const struct sieve_runtime_env *renv,
					   sieve_size_t *address);

const struct sieve_operation_def test_message_mailbox_operation = {
	.mnemonic = "TEST_MESSAGE_MAILBOX",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_MESSAGE_MAILBOX,
	.dump = cmd_test_message_mailbox_operation_dump,
	.execute = cmd_test_message_mailbox_operation_execute,
};

/* Test_message_print operation */

static bool
cmd_test_message_print_operation_dump(const struct sieve_dumptime_env *denv,
				      sieve_size_t *address);
static int
cmd_test_message_print_operation_execute(const struct sieve_runtime_env *renv,
					 sieve_size_t *address);

const struct sieve_operation_def test_message_print_operation = {
	.mnemonic = "TEST_MESSAGE_PRINT",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_MESSAGE_PRINT,
	.dump = cmd_test_message_print_operation_dump,
	.execute = cmd_test_message_print_operation_execute,
};

/*
 * Compiler context data
 */

enum test_message_source {
	MSG_SOURCE_SMTP,
	MSG_SOURCE_MAILBOX,
	MSG_SOURCE_LAST,
};

const struct sieve_operation_def *test_message_operations[] = {
	&test_message_smtp_operation,
	&test_message_mailbox_operation,
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

static bool
cmd_test_message_validate_smtp_tag(struct sieve_validator *valdtr,
				   struct sieve_ast_argument **arg,
				   struct sieve_command *cmd);
static bool
cmd_test_message_validate_folder_tag(struct sieve_validator *valdtr,
				     struct sieve_ast_argument **arg,
				     struct sieve_command *cmd);

static const struct sieve_argument_def test_message_smtp_tag = {
	.identifier = "smtp",
	.validate = cmd_test_message_validate_smtp_tag,
};

static const struct sieve_argument_def test_message_folder_tag = {
	.identifier = "folder",
	.validate = cmd_test_message_validate_folder_tag,
};

static bool
cmd_test_message_registered(struct sieve_validator *valdtr,
			    const struct sieve_extension *ext,
			    struct sieve_command_registration *cmd_reg)
{
	/* Register our tags */
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &test_message_folder_tag, 0);
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &test_message_smtp_tag, 0);
	return TRUE;
}

static struct cmd_test_message_context_data *
cmd_test_message_validate_tag(struct sieve_validator *valdtr,
			      struct sieve_ast_argument **arg,
			      struct sieve_command *cmd)
{
	struct cmd_test_message_context_data *ctx_data =
		(struct cmd_test_message_context_data *)cmd->data;

	if (ctx_data != NULL) {
		sieve_argument_validate_error(valdtr, *arg,
					      CMD_TEST_MESSAGE_ERROR_DUP_TAG);
		return NULL;
	}

	ctx_data = p_new(sieve_command_pool(cmd),
			 struct cmd_test_message_context_data, 1);
	cmd->data = ctx_data;

	/* Delete this tag */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	return ctx_data;
}

static bool
cmd_test_message_validate_smtp_tag(struct sieve_validator *valdtr,
				   struct sieve_ast_argument **arg,
				   struct sieve_command *cmd)
{
	struct cmd_test_message_context_data *ctx_data =
		cmd_test_message_validate_tag(valdtr, arg, cmd);

	/* Return value is NULL on error */
	if (ctx_data == NULL)
		return FALSE;

	/* Assign chosen message source */
	ctx_data->msg_source = MSG_SOURCE_SMTP;

	return TRUE;
}

static bool
cmd_test_message_validate_folder_tag(struct sieve_validator *valdtr,
				     struct sieve_ast_argument **arg,
				     struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct cmd_test_message_context_data *ctx_data =
		cmd_test_message_validate_tag(valdtr, arg, cmd);

	/* Return value is NULL on error */
	if (ctx_data == NULL)
		return FALSE;

	/* Assign chose message source */
	ctx_data->msg_source = MSG_SOURCE_MAILBOX;

	/* Check syntax:
	 *   :folder string
	 */
	if (!sieve_validate_tag_parameter(valdtr, cmd, tag, *arg, NULL, 0,
					  SAAT_STRING, FALSE)) {
		return FALSE;
	}

	/* Check name validity when folder argument is not a variable */
	if ( sieve_argument_is_string_literal(*arg) ) {
		const char *folder = sieve_ast_argument_strc(*arg), *error;

		if ( !sieve_mailbox_check_name(folder, &error) ) {
			sieve_command_validate_error(
				valdtr, cmd, "%s command: "
				"invalid mailbox `%s' specified: %s",
				sieve_command_identifier(cmd),
				str_sanitize(folder, 256), error);
			return FALSE;
		}
	}

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/*
 * Validation
 */

static bool
cmd_test_message_validate(struct sieve_validator *valdtr,
			  struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	if (cmd->data == NULL) {
		sieve_command_validate_error(
			valdtr, cmd,
			"the test_message command requires either "
			"the :smtp or the :mailbox tag to be specified");
		return FALSE;
	}

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "index", 1,
						SAAT_NUMBER))
		return FALSE;

	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/*
 * Code generation
 */

static bool
cmd_test_message_generate(const struct sieve_codegen_env *cgenv,
			  struct sieve_command *cmd)
{
	struct cmd_test_message_context_data *ctx_data =
		(struct cmd_test_message_context_data *)cmd->data;

	i_assert(ctx_data->msg_source < MSG_SOURCE_LAST);

	/* Emit operation */
	sieve_operation_emit(cgenv->sblock, cmd->ext,
			     test_message_operations[ctx_data->msg_source]);

	/* Emit is_test flag */
	sieve_binary_emit_byte(cgenv->sblock,
			       (cmd->ast_node->type == SAT_TEST ? 1 : 0));

 	/* Generate arguments */
	if (!sieve_generate_arguments(cgenv, cmd, NULL))
		return FALSE;

	return TRUE;
}

static bool
cmd_test_message_print_generate(const struct sieve_codegen_env *cgenv,
				struct sieve_command *cmd)
{
	/* Emit operation */
	sieve_operation_emit(cgenv->sblock, cmd->ext,
			     &test_message_print_operation);
	return TRUE;
}

/*
 * Code dump
 */

static bool
cmd_test_message_smtp_operation_dump(const struct sieve_dumptime_env *denv,
				     sieve_size_t *address)
{
	unsigned int is_test;

	if (!sieve_binary_read_byte(denv->sblock, address, &is_test))
		return FALSE;

	sieve_code_dumpf(denv, "TEST_MESSAGE_SMTP (%s):",
			 (is_test > 0 ? "TEST" : "COMMAND"));

	sieve_code_descend(denv);

	return sieve_opr_number_dump(denv, address, "index");
}

static bool
cmd_test_message_mailbox_operation_dump(const struct sieve_dumptime_env *denv,
					sieve_size_t *address)
{
	unsigned int is_test;

	if (!sieve_binary_read_byte(denv->sblock, address, &is_test))
		return FALSE;

	sieve_code_dumpf(denv, "TEST_MESSAGE_MAILBOX (%s):",
			 (is_test > 0 ? "TEST" : "COMMAND"));

	sieve_code_descend(denv);

	return (sieve_opr_string_dump(denv, address, "folder") &&
		sieve_opr_number_dump(denv, address, "index"));
}

static bool
cmd_test_message_print_operation_dump(const struct sieve_dumptime_env *denv,
				      sieve_size_t *address ATTR_UNUSED)
{
	sieve_code_dumpf(denv, "TEST_MESSAGE_PRINT");

	return TRUE;
}

/*
 * Intepretation
 */

static int
cmd_test_message_smtp_operation_execute(const struct sieve_runtime_env *renv,
					sieve_size_t *address)
{
	sieve_number_t msg_index;
	unsigned int is_test = 0;
	bool result;
	int ret;

	/*
	 * Read operands
	 */

	/* Is test */

	if (!sieve_binary_read_byte(renv->sblock, address, &is_test)) {
		sieve_runtime_trace_error(renv, "invalid is_test flag");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Index */

	ret = sieve_opr_number_read(renv, address, "index", &msg_index);
	if (ret <= 0)
		return ret;

	/*
	 * Perform operation
	 */

	if (is_test > 0) {
		if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_TESTS)) {
			sieve_runtime_trace(
				renv, 0, "testsuite: test_message test");
			sieve_runtime_trace_descend(renv);
			sieve_runtime_trace(
				renv, 0,
				"check and retrieve smtp message [index=%llu]",
				(unsigned long long)msg_index);
		}
	} else {
		if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_COMMANDS)) {
			sieve_runtime_trace(
				renv, 0, "testsuite: test_message command");
			sieve_runtime_trace_descend(renv);
			sieve_runtime_trace(
				renv, 0,
				"retrieve smtp message [index=%llu]",
				(unsigned long long)msg_index);
		}
	}

	result = testsuite_smtp_get(renv, msg_index);

	if (is_test > 0) {
		sieve_interpreter_set_test_result(renv->interp, result);
		return SIEVE_EXEC_OK;
	}

	if (!result) {
		return testsuite_test_failf(
			renv, "no outgoing SMTP message with index %llu",
			(unsigned long long)msg_index);
	}

	return SIEVE_EXEC_OK;
}

static int
cmd_test_message_mailbox_operation_execute(const struct sieve_runtime_env *renv,
					   sieve_size_t *address)
{
	string_t *folder;
	sieve_number_t msg_index;
	unsigned int is_test = 0;
	bool result;
	const char *error;
	int ret;

	/*
	 * Read operands
	 */

	/* Is test */
	if (!sieve_binary_read_byte(renv->sblock, address, &is_test)) {
		sieve_runtime_trace_error(renv, "invalid is_test flag");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Folder */
	ret = sieve_opr_string_read(renv, address, "folder", &folder);
	if (ret <= 0)
		return ret;

	/* Index */
	ret = sieve_opr_number_read(renv, address, "index", &msg_index);
	if (ret <= 0)
		return ret;

	if (!sieve_mailbox_check_name(str_c(folder), &error)) {
		return testsuite_test_failf(
			renv, "invalid mailbox `%s' specified: %s",
			str_c(folder), error);
	}

	/*
	 * Perform operation
	 */

	if (is_test > 0) {
		if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_TESTS)) {
			sieve_runtime_trace(
				renv, 0, "testsuite: test_message test");
			sieve_runtime_trace_descend(renv);
			sieve_runtime_trace(
				renv, 0, "check and retrieve mailbox message "
				"[mailbox=`%s' index=%llu]",
				str_c(folder), (unsigned long long)msg_index);
		}
	} else {
		if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_COMMANDS)) {
			sieve_runtime_trace(
				renv, 0, "testsuite: test_message command");
			sieve_runtime_trace_descend(renv);
			sieve_runtime_trace(
				renv, 0, "retrieve mailbox message "
				"[mailbox=`%s' index=%llu]",
				str_c(folder), (unsigned long long)msg_index);
		}
	}

	result = testsuite_mailstore_mail_index(renv, str_c(folder), msg_index);

	if (is_test > 0) {
		sieve_interpreter_set_test_result(renv->interp, result);
		return SIEVE_EXEC_OK;
	}

	if (!result) {
		return testsuite_test_failf(
			renv, "no message in folder '%s' with index %llu",
			str_c(folder), (unsigned long long)msg_index);
	}

	return SIEVE_EXEC_OK;
}

static int
cmd_test_message_print_operation_execute(const struct sieve_runtime_env *renv,
					 sieve_size_t *address ATTR_UNUSED)
{
	struct mail *mail = sieve_message_get_mail(renv->msgctx);
	struct istream *input;
	const unsigned char *data;
	size_t size;

	if (mail_get_stream(mail, NULL, NULL, &input) < 0) {
		sieve_runtime_error(renv, NULL,	"test_message_print: "
				    "failed to read current message");
		return SIEVE_EXEC_OK;
	}

	printf("\n--MESSAGE: \n");

	/* Pipe the message to the outgoing SMTP transport */
	while (i_stream_read_more(input, &data, &size) > 0) {
		ssize_t wret;

		wret = write(1, data, size);
		if (wret <= 0)
			break;
		i_stream_skip(input, wret);
	}
	printf("\n--MESSAGE--\n");

	return SIEVE_EXEC_OK;
}
