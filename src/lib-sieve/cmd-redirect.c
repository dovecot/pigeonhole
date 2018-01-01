/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "strfuncs.h"
#include "istream.h"
#include "istream-header-filter.h"
#include "ostream.h"
#include "mail-storage.h"

#include "rfc2822.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-address.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"
#include "sieve-result.h"
#include "sieve-smtp.h"
#include "sieve-message.h"

#include <stdio.h>

/*
 * Redirect command
 *
 * Syntax
 *   redirect <address: string>
 */

static bool cmd_redirect_validate
	(struct sieve_validator *validator, struct sieve_command *cmd);
static bool cmd_redirect_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd);

const struct sieve_command_def cmd_redirect = {
	.identifier = "redirect",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_redirect_validate,
	.generate = cmd_redirect_generate
};

/*
 * Redirect operation
 */

static bool cmd_redirect_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_redirect_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def cmd_redirect_operation = {
	.mnemonic = "REDIRECT",
	.code = SIEVE_OPERATION_REDIRECT,
	.dump = cmd_redirect_operation_dump,
	.execute = cmd_redirect_operation_execute
};

/*
 * Redirect action
 */

static bool act_redirect_equals
	(const struct sieve_script_env *senv, const struct sieve_action *act1,
		const struct sieve_action *act2);
static int act_redirect_check_duplicate
	(const struct sieve_runtime_env *renv,
		const struct sieve_action *act,
		const struct sieve_action *act_other);
static void act_redirect_print
	(const struct sieve_action *action, const struct sieve_result_print_env *rpenv,
		bool *keep);
static int act_redirect_commit
	(const struct sieve_action *action, const struct sieve_action_exec_env *aenv,
		void *tr_context, bool *keep);

const struct sieve_action_def act_redirect = {
	.name = "redirect",
	.flags = SIEVE_ACTFLAG_TRIES_DELIVER,
	.equals = act_redirect_equals,
	.check_duplicate = act_redirect_check_duplicate,
	.print = act_redirect_print,
	.commit = act_redirect_commit
};

/*
 * Validation
 */

static bool cmd_redirect_validate
(struct sieve_validator *validator, struct sieve_command *cmd)
{
	struct sieve_instance *svinst = sieve_validator_svinst(validator);
	struct sieve_ast_argument *arg = cmd->first_positional;

	/* Check and activate address argument */

	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "address", 1, SAAT_STRING) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(validator, cmd, arg, FALSE) )
		return FALSE;

	/* We can only assess the validity of the outgoing address when it is
	 * a string literal. For runtime-generated strings this needs to be
	 * done at runtime.
	 */
	if ( sieve_argument_is_string_literal(arg) ) {
		string_t *raw_address = sieve_ast_argument_str(arg);
		const char *error;
		bool result;

		T_BEGIN {
			/* Parse the address */
			result = sieve_address_validate_str(raw_address, &error);
			if ( !result ) {
				sieve_argument_validate_error(validator, arg,
					"specified redirect address '%s' is invalid: %s",
					str_sanitize(str_c(raw_address),128), error);
			}
		} T_END;

		return result;
	}

	if ( svinst->max_redirects == 0 ) {
		sieve_command_validate_error(validator, cmd,
			"local policy prohibits the use of a redirect action");
		return FALSE;
	}
	return TRUE;
}



/*
 * Code generation
 */

static bool cmd_redirect_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, NULL,  &cmd_redirect_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, cmd, NULL);
}

/*
 * Code dump
 */

static bool cmd_redirect_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "REDIRECT");
	sieve_code_descend(denv);

	if ( sieve_action_opr_optional_dump(denv, address, NULL) != 0 )
		return FALSE;

	return sieve_opr_string_dump(denv, address, "address");
}

/*
 * Code execution
 */

static int cmd_redirect_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_instance *svinst = renv->svinst;
	struct sieve_side_effects_list *slist = NULL;
	string_t *redirect;
	const struct smtp_address *to_address;
	const char *error;
	int ret;

	/*
	 * Read data
	 */

	/* Optional operands (side effects only) */
	if ( sieve_action_opr_optional_read(renv, address, NULL, &ret, &slist) != 0 )
		return ret;

	/* Read the address */
	if ( (ret=sieve_opr_string_read
		(renv, address, "address", &redirect)) <= 0 )
		return ret;

	/*
	 * Perform operation
	 */

	/* Parse the address */
	to_address = sieve_address_parse_str(redirect, &error);
	if ( to_address == NULL ) {
		sieve_runtime_error(renv, NULL,
			"specified redirect address '%s' is invalid: %s",
			str_sanitize(str_c(redirect),128), error);
		return SIEVE_EXEC_FAILURE;
	}

	if ( svinst->max_redirects == 0 ) {
		sieve_runtime_error(renv, NULL,
			"local policy prohibits the use of a redirect action");
		return SIEVE_EXEC_FAILURE;
	}

	if ( sieve_runtime_trace_active(renv, SIEVE_TRLVL_ACTIONS) ) {
		sieve_runtime_trace(renv, 0, "redirect action");
		sieve_runtime_trace_descend(renv);
		sieve_runtime_trace(renv, 0, "forward message to address %s",
			smtp_address_encode_path(to_address));
	}

	/* Add redirect action to the result */

	return sieve_act_redirect_add_to_result
		(renv, slist, to_address);
}

/*
 * Action implementation
 */

static bool act_redirect_equals
(const struct sieve_script_env *senv ATTR_UNUSED,
	const struct sieve_action *act1, const struct sieve_action *act2)
{
	struct act_redirect_context *rd_ctx1 =
		(struct act_redirect_context *) act1->context;
	struct act_redirect_context *rd_ctx2 =
		(struct act_redirect_context *) act2->context;

	/* Address is already normalized */
	return ( smtp_address_equals
		(rd_ctx1->to_address, rd_ctx2->to_address) );
}

static int act_redirect_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action *act,
	const struct sieve_action *act_other)
{
	return ( act_redirect_equals
		(renv->scriptenv, act, act_other) ? 1 : 0 );
}

static void act_redirect_print
(const struct sieve_action *action,
	const struct sieve_result_print_env *rpenv, bool *keep)
{
	struct act_redirect_context *ctx =
		(struct act_redirect_context *) action->context;

	sieve_result_action_printf(rpenv, "redirect message to: %s",
		smtp_address_encode_path(ctx->to_address));

	*keep = FALSE;
}

static int act_redirect_send
(const struct sieve_action_exec_env *aenv, struct mail *mail,
	struct act_redirect_context *ctx, const char *new_msg_id)
	ATTR_NULL(4)
{
	static const char *hide_headers[] =
		{ "Return-Path", "X-Sieve", "X-Sieve-Redirected-From" };
	struct sieve_instance *svinst = aenv->svinst;
	struct sieve_message_context *msgctx = aenv->msgctx;
	const struct sieve_script_env *senv = aenv->scriptenv;
	struct sieve_address_source env_from = svinst->redirect_from;
	struct istream *input;
	struct ostream *output;
	const struct smtp_address *sender;
	const char *error;
	struct sieve_smtp_context *sctx;
	int ret;

	/* Just to be sure */
	if ( !sieve_smtp_available(senv) ) {
		sieve_result_global_warning
			(aenv, "redirect action has no means to send mail.");
		return SIEVE_EXEC_FAILURE;
	}

	if (mail_get_stream(mail, NULL, NULL, &input) < 0) {
		return sieve_result_mail_error(aenv, mail,
			"redirect action: failed to read input message");
	}

	/* Determine which sender to use

	   From RFC 5228, Section 4.2:

		 The envelope sender address on the outgoing message is chosen by the
		 sieve implementation.  It MAY be copied from the message being
		 processed.  However, if the message being processed has an empty
		 envelope sender address the outgoing message MUST also have an empty
		 envelope sender address.  This last requirement is imposed to prevent
		 loops in the case where a message is redirected to an invalid address
		 when then returns a delivery status notification that also ends up
		 being redirected to the same invalid address.
	 */
	if ( (aenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0 ) {
		/* Envelope available */
		sender = sieve_message_get_sender(msgctx);
		if ( sender != NULL &&
			sieve_address_source_get_address(&env_from, svinst,
				senv, msgctx, aenv->flags, &sender) < 0 )
			sender = NULL;
	} else {
		/* No envelope available */
		if ( (ret=sieve_address_source_get_address(&env_from, svinst,
			senv, msgctx, aenv->flags, &sender)) < 0 ) {
			sender = NULL;
		} else if ( ret == 0 ) {
			sender = svinst->user_email;
		}
	}

	/* Open SMTP transport */
	sctx = sieve_smtp_start_single(senv, ctx->to_address, sender, &output);

	/* Remove unwanted headers */
	input = i_stream_create_header_filter
		(input, HEADER_FILTER_EXCLUDE | HEADER_FILTER_NO_CR, hide_headers,
			N_ELEMENTS(hide_headers), *null_header_filter_callback, (void *)NULL);

	T_BEGIN {
		string_t *hdr = t_str_new(256);
		const struct smtp_address *user_email;

		/* Prepend sieve headers (should not affect signatures) */
		rfc2822_header_append(hdr,
			"X-Sieve", SIEVE_IMPLEMENTATION, FALSE, NULL);
		if ( svinst->user_email == NULL &&
			(aenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0 )
			user_email = sieve_message_get_final_recipient(msgctx);
		else
			user_email = sieve_get_user_email(aenv->svinst);
		if ( user_email != NULL ) {
			rfc2822_header_append(hdr, "X-Sieve-Redirected-From",
				smtp_address_encode(user_email), FALSE, NULL);
		}

		/* Add new Message-ID if message doesn't have one */
		if ( new_msg_id != NULL )
			rfc2822_header_write(hdr, "Message-ID", new_msg_id);

		o_stream_nsend(output, str_data(hdr), str_len(hdr));
	} T_END;

	o_stream_nsend_istream(output, input);

	if (input->stream_errno != 0) {
		sieve_result_critical(aenv,
			"redirect action: failed to read input message",
			"redirect action: read(%s) failed: %s",
			i_stream_get_name(input),
			i_stream_get_error(input));
		i_stream_unref(&input);
		return SIEVE_EXEC_TEMP_FAILURE;
	}
  i_stream_unref(&input);

	/* Close SMTP transport */
	if ( (ret=sieve_smtp_finish(sctx, &error)) <= 0 ) {
		if ( ret < 0 ) {
			sieve_result_global_error(aenv,
				"failed to redirect message to <%s>: %s "
				"(temporary failure)",
				smtp_address_encode(ctx->to_address),
				str_sanitize(error, 512));
			return SIEVE_EXEC_TEMP_FAILURE;
		}

		sieve_result_global_log_error(aenv,
			"failed to redirect message to <%s>: %s "
			"(permanent failure)",
			smtp_address_encode(ctx->to_address),
			str_sanitize(error, 512));
		return SIEVE_EXEC_FAILURE;
	}

	return SIEVE_EXEC_OK;
}

static int act_redirect_commit
(const struct sieve_action *action,
	const struct sieve_action_exec_env *aenv, void *tr_context ATTR_UNUSED,
	bool *keep)
{
	struct sieve_instance *svinst = aenv->svinst;
	struct act_redirect_context *ctx =
		(struct act_redirect_context *) action->context;
	struct sieve_message_context *msgctx = aenv->msgctx;
	struct mail *mail =	( action->mail != NULL ?
		action->mail : sieve_message_get_mail(msgctx) );
	const struct sieve_message_data *msgdata = aenv->msgdata;
	const struct sieve_script_env *senv = aenv->scriptenv;
	const struct smtp_address *recipient;
	const char *msg_id = msgdata->id, *new_msg_id = NULL;
	const char *dupeid, *resent_id = NULL;
	const char *list_id = NULL;
	int ret;

	/*
	 * Prevent mail loops
	 */

	/* Read identifying headers */
	if ( mail_get_first_header
		(msgdata->mail, "resent-message-id", &resent_id) < 0 ) {
		return sieve_result_mail_error(aenv, mail,
			"failed to read header field `resent-message-id'");
	}
	if ( resent_id == NULL ) {
		if ( mail_get_first_header
			(msgdata->mail, "resent-from", &resent_id) < 0 ) {
			return sieve_result_mail_error(aenv, mail,
				"failed to read header field `resent-from'");
		}
	}
	if ( mail_get_first_header
		(msgdata->mail, "list-id", &list_id) < 0 ) {
		return sieve_result_mail_error(aenv, mail,
			"failed to read header field `list-id'");
	}

	/* Create Message-ID for the message if it has none */
	if ( msg_id == NULL ) {	
		msg_id = new_msg_id =
			sieve_message_get_new_id(aenv->svinst);
	}

	if ( (aenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0 )
		recipient = sieve_message_get_orig_recipient(msgctx);
	else
		recipient = sieve_get_user_email(aenv->svinst);

	/* Base the duplicate ID on:
	   - the message id
	   - the recipient running this Sieve script
	   - redirect target address
	   - if this message is resent: the message-id or from-address of
		   the original message
	   - if the message came through a mailing list: the mailinglist ID
	 */
	dupeid = t_strdup_printf("%s-%s-%s-%s-%s", msg_id,
		(recipient != NULL ? smtp_address_encode(recipient) : ""),
		smtp_address_encode(ctx->to_address),
		(resent_id != NULL ? resent_id : ""),
		(list_id != NULL ? list_id : ""));

	/* Check whether we've seen this message before */
	if (sieve_action_duplicate_check
		(senv, dupeid, strlen(dupeid))) {
		sieve_result_global_log(aenv,
			"discarded duplicate forward to <%s>",
			smtp_address_encode(ctx->to_address));
		*keep = FALSE;
		return SIEVE_EXEC_OK;
	}

	/*
	 * Try to forward the message
	 */

	if ( (ret=act_redirect_send
		(aenv, mail, ctx, new_msg_id)) == SIEVE_EXEC_OK) {

		/* Mark this message id as forwarded to the specified destination */
		sieve_action_duplicate_mark(senv, dupeid, strlen(dupeid),
			ioloop_time + svinst->redirect_duplicate_period);

		sieve_result_global_log(aenv, "forwarded to <%s>",
			smtp_address_encode(ctx->to_address));

		/* Indicate that message was successfully forwarded */
		aenv->exec_status->message_forwarded = TRUE;

		/* Cancel implicit keep */
		*keep = FALSE;

		return SIEVE_EXEC_OK;
	}

	return ret;
}


