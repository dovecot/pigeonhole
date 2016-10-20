/* Copyright (c) 2016 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "str.h"
#include "ioloop.h"
#include "hostpid.h"
#include "str-sanitize.h"
#include "istream.h"
#include "ostream.h"
#include "message-date.h"
#include "message-size.h"
#include "mail-storage.h"

#include "rfc2822.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-address.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-message.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"
#include "sieve-address.h"
#include "sieve-message.h"
#include "sieve-smtp.h"

#include "ext-vnd-report-common.h"

#include <ctype.h>

/* Report command
 *
 * Syntax:
 *    report [:headers_only] <feedback-type: string>
 *           <message: string> <address: string>
 *
 */

static bool cmd_report_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool cmd_report_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool cmd_report_generate
	(const struct sieve_codegen_env *cgenv,
		struct sieve_command *ctx);

const struct sieve_command_def cmd_report = {
	.identifier = "report",
	.type = SCT_COMMAND,
	.positional_args = 3,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = cmd_report_registered,
	.validate = cmd_report_validate,
	.generate = cmd_report_generate
};

/*
 * Tagged arguments
 */

static const struct sieve_argument_def report_headers_only_tag = {
	.identifier = "headers_only"
};

/*
 * Report operation
 */

static bool cmd_report_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_report_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def report_operation = {
	.mnemonic = "REPORT",
	.ext_def = &vnd_report_extension,
	.code = 0,
	.dump = cmd_report_operation_dump,
	.execute = cmd_report_operation_execute
};

/* Codes for optional operands */

enum cmd_report_optional {
  OPT_END,
  OPT_HEADERS_ONLY
};

/*
 * Report action
 */

/* Forward declarations */

static int act_report_check_duplicate
	(const struct sieve_runtime_env *renv,
		const struct sieve_action *act,
		const struct sieve_action *act_other);
static void act_report_print
	(const struct sieve_action *action, const struct sieve_result_print_env *rpenv,
		bool *keep);
static int act_report_commit
	(const struct sieve_action *action,	const struct sieve_action_exec_env *aenv,
		void *tr_context, bool *keep);

/* Action object */

const struct sieve_action_def act_report = {
	.name = "report",
	.check_duplicate = act_report_check_duplicate,
	.print = act_report_print,
	.commit = act_report_commit
};

/* Action data */

struct act_report_data {
	const char *feedback_type;
	const char *message;
	const char *to_address;
	unsigned int headers_only:1;
};

/*
 * Command registration
 */

static bool cmd_report_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &report_headers_only_tag, OPT_HEADERS_ONLY);

	return TRUE;
}

/*
 * Command validation
 */

static bool cmd_report_validate
(struct sieve_validator *valdtr, struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	/* type */
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "feedback-type", 1, SAAT_STRING) ) {
		return FALSE;
	}
	if ( !sieve_validator_argument_activate
		(valdtr, cmd, arg, FALSE) )
		return FALSE;

	if ( sieve_argument_is_string_literal(arg) ) {
		string_t *fbtype = sieve_ast_argument_str(arg);
		const char *feedback_type;

		T_BEGIN {
			/* Check feedback type */
			feedback_type = ext_vnd_report_parse_feedback_type
				(str_c(fbtype));

			if ( feedback_type == NULL ) {
				sieve_argument_validate_error(valdtr, arg,
					"specified feedback type `%s' is invalid",
					str_sanitize(str_c(fbtype),128));
			}
		} T_END;

		if ( feedback_type == NULL )
			return FALSE;
	}
	arg = sieve_ast_argument_next(arg);

	/* message */
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "message", 2, SAAT_STRING) ) {
		return FALSE;
	}
	if ( !sieve_validator_argument_activate
		(valdtr, cmd, arg, FALSE) )
		return FALSE;
	arg = sieve_ast_argument_next(arg);

	/* address */
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "address", 3, SAAT_STRING) ) {
		return FALSE;
	}
	if ( !sieve_validator_argument_activate
		(valdtr, cmd, arg, FALSE) )
		return FALSE;

	/* We can only assess the validity of the outgoing address when it is
	 * a string literal. For runtime-generated strings this needs to be
	 * done at runtime.
	 */
	if ( sieve_argument_is_string_literal(arg) ) {
		string_t *address = sieve_ast_argument_str(arg);
		const char *error;
		const char *norm_address;

		T_BEGIN {
			/* Verify and normalize the address to 'local_part@domain' */
			norm_address = sieve_address_normalize(address, &error);

			if ( norm_address == NULL ) {
				sieve_argument_validate_error(valdtr, arg,
					"specified redirect address `%s' is invalid: %s",
					str_sanitize(str_c(address),128), error);
			} else {
				/* Replace string literal in AST */
				sieve_ast_argument_string_setc(arg, norm_address);
			}
		} T_END;

		return ( norm_address != NULL );
	}

	return TRUE;
}

/*
 * Code generation
 */

static bool cmd_report_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &report_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, cmd, NULL);
}

/*
 * Code dump
 */

static bool cmd_report_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "REPORT");
	sieve_code_descend(denv);

	/* Dump optional operands */
	for (;;) {
		int opt;

		if ( (opt=sieve_opr_optional_dump
			(denv, address, &opt_code)) < 0 )
			return FALSE;

		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case OPT_HEADERS_ONLY:
			sieve_code_dumpf(denv, "headers_only");
			break;
		default:
			return FALSE;
		}
	}

	return
		sieve_opr_string_dump(denv, address, "feedback-type") &&
		sieve_opr_string_dump(denv, address, "message") &&
		sieve_opr_string_dump(denv, address, "address");
}

/*
 * Code execution
 */


static int cmd_report_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct act_report_data *act;
	string_t *fbtype, *message, *to_address;
	const char *norm_address, *feedback_type, *error;
	int opt_code = 0, ret = 0;
	bool headers_only = FALSE;
	pool_t pool;

	/*
	 * Read operands
	 */

	/* Optional operands */

	for (;;) {
		int opt;

		if ( (opt=sieve_opr_optional_read(renv, address, &opt_code)) < 0 )
			return SIEVE_EXEC_BIN_CORRUPT;

		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case OPT_HEADERS_ONLY:
			headers_only = TRUE;
			break;
		default:
			sieve_runtime_trace_error(renv, "unknown optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	}

	/* Fixed operands */

	if ( (ret=sieve_opr_string_read
		(renv, address, "feedback-type", &fbtype)) <= 0 )
		return ret;

	if ( (ret=sieve_opr_string_read
		(renv, address, "message", &message)) <= 0 )
		return ret;

	if ( (ret=sieve_opr_string_read
		(renv, address, "address", &to_address)) <= 0 )
		return ret;

	/*
	 * Perform operation
	 */

	/* Verify and trim feedback type */
	feedback_type = ext_vnd_report_parse_feedback_type(str_c(fbtype));
	if ( feedback_type == NULL ) {
		sieve_runtime_error(renv, NULL,
			"specified report feedback type `%s' is invalid",
			str_sanitize(str_c(fbtype), 256));
		return SIEVE_EXEC_FAILURE;
	}

	/* Verify and normalize the address to 'local_part@domain' */
	norm_address = sieve_address_normalize(to_address, &error);
	if ( norm_address == NULL ) {
		sieve_runtime_error(renv, NULL,
			"specified report address `%s' is invalid: %s",
			str_sanitize(str_c(to_address), 256), error);
		return SIEVE_EXEC_FAILURE;
	}

	/* Trace */
	if ( sieve_runtime_trace_active(renv, SIEVE_TRLVL_ACTIONS) ) {
		sieve_runtime_trace(renv, 0, "report action");
		sieve_runtime_trace_descend(renv);
		sieve_runtime_trace(renv, 0,
			"report incoming message as `%s' to address `%s'",
			str_sanitize(str_c(fbtype), 32),
			str_sanitize(norm_address, 80));
	}

	/* Add report action to the result */

	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_report_data, 1);
	act->headers_only = headers_only;
	act->feedback_type = p_strdup(pool, feedback_type);
	act->message = p_strdup(pool, str_c(message));
	act->to_address = p_strdup(pool, norm_address);

	if ( sieve_result_add_action(renv,
		this_ext, &act_report, NULL, (void *) act, 0, TRUE) < 0 )
		return SIEVE_EXEC_FAILURE;

	return SIEVE_EXEC_OK;
}

/*
 * Action
 */

/* Runtime verification */

static bool act_report_equals
(const struct sieve_script_env *senv ATTR_UNUSED,
	const struct sieve_action *act1,
	const struct sieve_action *act2)
{
	struct act_report_data *rdd1 =
		(struct act_report_data *) act1->context;
	struct act_report_data *rdd2 =
		(struct act_report_data *) act2->context;

	/* Address is already normalized */
	return ( sieve_address_compare
		(rdd1->to_address, rdd2->to_address, TRUE) == 0 );
}

static int act_report_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action *act,
	const struct sieve_action *act_other)
{
	return ( act_report_equals
		(renv->scriptenv, act, act_other) ? 1 : 0 );
}

/* Result printing */

static void act_report_print
(const struct sieve_action *action,
	const struct sieve_result_print_env *rpenv,
	bool *keep ATTR_UNUSED)
{
	const struct act_report_data *rdd =
		(struct act_report_data *) action->context;

	sieve_result_action_printf(rpenv,
		"report incoming message as `%s' to: %s",
		str_sanitize(rdd->feedback_type, 32),
		str_sanitize(rdd->to_address, 256));
}

/* Result execution */

static bool _contains_8bit(const char *msg)
{
	const unsigned char *s = (const unsigned char *)msg;

	for (; *s != '\0'; s++) {
		if ((*s & 0x80) != 0)
			return TRUE;
	}
	return FALSE;
}

static int act_report_send
(const struct sieve_action_exec_env *aenv,
	const struct ext_report_config *config,
	const struct act_report_data *act)
{
	struct sieve_instance *svinst = aenv->svinst;
	struct sieve_message_context *msgctx = aenv->msgctx;
	const struct sieve_script_env *senv = aenv->scriptenv;
	const struct sieve_message_data *msgdata = aenv->msgdata;
	struct sieve_address_source report_from = config->report_from;
	struct sieve_smtp_context *sctx;
	struct istream *input;
	struct ostream *output;
	string_t *msg;
	const char *const *headers;
	const char *outmsgid, *boundary, *error, *subject, *from, *user;
	int ret;

	/* Just to be sure */
	if ( !sieve_smtp_available(senv) ) {
		sieve_result_global_warning(aenv,
			"report action has no means to send mail");
		return SIEVE_EXEC_OK;
	}

	/* Make sure we have a subject for our report */
	if ( mail_get_headers_utf8
		(msgdata->mail, "subject", &headers) < 0 ) {
		return sieve_result_mail_error(aenv, msgdata->mail,
			"report action: "
			"failed to read header field `subject'");
	}
	if ( headers[0] != NULL ) {
		subject = t_strconcat("Report: ", headers[0], NULL);
	}	else {
		subject = "Report: (message without subject)";
	}

	/* Determine from address */
	if ( report_from.type == SIEVE_ADDRESS_SOURCE_POSTMASTER ) {
		report_from.type = SIEVE_ADDRESS_SOURCE_DEFAULT;
		report_from.address = NULL;
	}
	if ( (ret=sieve_address_source_get_address
		(&report_from, svinst, senv, msgctx,
			aenv->flags, &from)) <= 0 || from == NULL || *from == '\0') {
		from = t_strdup_printf
			("Postmaster <%s>", senv->postmaster_address);
	}

	/* Start message */
	sctx = sieve_smtp_start_single
		(senv, act->to_address, NULL, &output);

	outmsgid = sieve_message_get_new_id(aenv->svinst);
	boundary = t_strdup_printf("%s/%s", my_pid, svinst->hostname);

	/* Compose main report headers */
	msg = t_str_new(512);
	rfc2822_header_write(msg, "X-Sieve", SIEVE_IMPLEMENTATION);
	rfc2822_header_write(msg, "Message-ID", outmsgid);
	rfc2822_header_write(msg, "Date", message_date_create(ioloop_time));

	rfc2822_header_write(msg, "From", from);
	rfc2822_header_printf(msg, "To", "<%s>", act->to_address);

	if ( _contains_8bit(subject) )
		rfc2822_header_utf8_printf(msg, "Subject", "%s", subject);
	else
		rfc2822_header_printf(msg, "Subject", "%s", subject);

	rfc2822_header_write(msg, "Auto-Submitted", "auto-generated (report)");

	rfc2822_header_write(msg, "MIME-Version", "1.0");
	rfc2822_header_printf(msg, "Content-Type",
		"multipart/report; report-type=feedback-report;\n"
		"boundary=\"%s\"", boundary);

	str_append(msg, "\r\nThis is a MIME-encapsulated message\r\n\r\n");

	/* Human-readable report */
	str_printfa(msg, "--%s\r\n", boundary);
	if (_contains_8bit(act->message)) {
		rfc2822_header_write(msg,
			"Content-Type", "text/plain; charset=utf-8");
		rfc2822_header_write(msg, "Content-Transfer-Encoding", "8bit");
	} else {
		rfc2822_header_write(msg,
			"Content-Type", "text/plain; charset=us-ascii");
		rfc2822_header_write(msg, "Content-Transfer-Encoding", "7bit");
	}
	rfc2822_header_write(msg, "Content-Disposition", "inline");

	str_printfa(msg, "\r\n%s\r\n\r\n", act->message);
	o_stream_send(output, str_data(msg), str_len(msg));

	/* Machine-readable report */
  str_truncate(msg, 0);
	str_printfa(msg, "--%s\r\n", boundary);
	rfc2822_header_write(msg,
		"Content-Type", "message/feedback-report");
	str_append(msg, "\r\n");

	rfc2822_header_write(msg,	"Version", "1");
	rfc2822_header_write(msg,
		"Feedback-Type", act->feedback_type);
	rfc2822_header_write(msg,	"User-Agent",
		PACKAGE_NAME "/" PACKAGE_VERSION " "
		PIGEONHOLE_NAME "/" PIGEONHOLE_VERSION);

	if ( (aenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0 ) {
		const char *sender, *orig_recipient;

		sender = sieve_message_get_sender(msgctx);
		orig_recipient = sieve_message_get_orig_recipient(msgctx);

		if (sender == NULL) {
			rfc2822_header_write(msg,
				"Original-Mail-From", "<>");
		} else {
			rfc2822_header_printf(msg,
				"Original-Mail-From", "<%s>", sender);
		}
		if (orig_recipient != NULL) {
			rfc2822_header_printf(msg,
				"Original-Rcpt-To", "<%s>", orig_recipient);
		}
	}
	if (svinst->user_email != NULL)
		user = sieve_address_to_string(svinst->user_email);
	else if ((aenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) != 0 ||
		(user=sieve_message_get_orig_recipient(msgctx)) == NULL)
		user = sieve_get_user_email(svinst);
	if (user != NULL) {
		rfc2822_header_printf(msg,
			"Dovecot-Reporting-User", "<%s>", user);
	}
	str_append(msg, "\r\n");

	o_stream_send(output, str_data(msg), str_len(msg));

	/* Original message */
  str_truncate(msg, 0);
	str_printfa(msg, "--%s\r\n", boundary);
	if (act->headers_only) {
		rfc2822_header_write(msg,
			"Content-Type", "text/rfc822-headers");
	} else {
		rfc2822_header_write(msg,
			"Content-Type", "message/rfc822");
	}
	rfc2822_header_write(msg,
		"Content-Disposition", "attachment");
	str_append(msg, "\r\n");
	o_stream_send(output, str_data(msg), str_len(msg));

	if (act->headers_only) {
		struct message_size hdr_size;
		ret = mail_get_hdr_stream(msgdata->mail, &hdr_size, &input);
		if (ret >= 0)
			input = i_stream_create_limit(input, hdr_size.physical_size);
	} else {
		ret = mail_get_stream(msgdata->mail, NULL, NULL, &input);
		if (ret >= 0)
			i_stream_ref(input);
	}
	if (ret < 0) {
		return sieve_result_mail_error(aenv, msgdata->mail,
			"report action: failed to read input message");
	}

  ret = o_stream_send_istream(output, input);

	/* blocking i/o required */
	i_assert( ret != 0 );

	if ( ret < 0 && input->stream_errno != 0 ) {
		/* Error; clean up */
		sieve_result_critical(aenv,
			"report action: failed to read input message",
			"report action: read(%s) failed: %s",
			i_stream_get_name(input),
			i_stream_get_error(input));
		i_stream_unref(&input);
		return SIEVE_EXEC_OK;
	}
	i_stream_unref(&input);

  str_truncate(msg, 0);
	if (!act->headers_only)
		str_printfa(msg, "\r\n");
	str_printfa(msg, "\r\n--%s--\r\n", boundary);
  o_stream_send(output, str_data(msg), str_len(msg));

	/* Finish sending message */
	if ( (ret=sieve_smtp_finish(sctx, &error)) <= 0 ) {
		if (ret < 0) {
			sieve_result_global_error(aenv,
				"failed to send `%s' report to <%s>: %s "
				"(temporary failure)",
				str_sanitize(act->feedback_type, 32),
				str_sanitize(act->to_address, 256),
				str_sanitize(error, 512));
		} else {
			sieve_result_global_log_error(aenv,
				"failed to send `%s' report to <%s>: %s "
				"(permanent failure)",
				str_sanitize(act->feedback_type, 32),
				str_sanitize(act->to_address, 256),
				str_sanitize(error, 512));
		}
	} else {
		sieve_result_global_log(aenv,
			"sent `%s' report to <%s>",
			str_sanitize(act->feedback_type, 32),
			str_sanitize(act->to_address, 256));
	}

	return SIEVE_EXEC_OK;
}

static int act_report_commit
(const struct sieve_action *action,
	const struct sieve_action_exec_env *aenv,
	void *tr_context ATTR_UNUSED,
	bool *keep ATTR_UNUSED)
{
	const struct sieve_extension *ext = action->ext;
	const struct ext_report_config *config =
		(const struct ext_report_config *) ext->context;
	const struct act_report_data *act =
		(const struct act_report_data *) action->context;
	int ret;

	T_BEGIN {
		ret = act_report_send(aenv, config, act);
	} T_END;

	if ( ret == SIEVE_EXEC_TEMP_FAILURE )
		return SIEVE_EXEC_TEMP_FAILURE;

	/* Ignore all other errors */
	return SIEVE_EXEC_OK;
}
