/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension reject
 * ----------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5429
 * Implementation: full
 * Status: testing
 *
 */

#include "lib.h"
#include "ioloop.h"
#include "hostpid.h"
#include "str-sanitize.h"
#include "message-date.h"
#include "message-size.h"
#include "istream.h"
#include "istream-header-filter.h"

#include "rfc2822.h"

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"
#include "sieve-message.h"
#include "sieve-smtp.h"

/*
 * Forward declarations
 */

static const struct sieve_command_def reject_command;
static const struct sieve_operation_def reject_operation;

static const struct sieve_command_def ereject_command;
static const struct sieve_operation_def ereject_operation;

/*
 * Extensions
 */

static bool
ext_reject_validator_validate(const struct sieve_extension *ext,
			      struct sieve_validator *valdtr, void *context,
			      struct sieve_ast_argument *require_arg,
			      bool required);
static int
ext_reject_interpreter_run(const struct sieve_extension *this_ext,
			   const struct sieve_runtime_env *renv,
			   void *context, bool deferred);

/* Reject */

static bool
ext_reject_validator_load(const struct sieve_extension *ext,
			  struct sieve_validator *valdtr);
static bool
ext_reject_interpreter_load(const struct sieve_extension *ext,
			    const struct sieve_runtime_env *renv,
			    sieve_size_t *address);

const struct sieve_extension_def reject_extension = {
	.name = "reject",
	.validator_load = ext_reject_validator_load,
	.interpreter_load = ext_reject_interpreter_load,
	SIEVE_EXT_DEFINE_OPERATION(reject_operation)
};
const struct sieve_validator_extension
reject_validator_extension = {
	.ext = &reject_extension,
	.validate = ext_reject_validator_validate
};
const struct sieve_interpreter_extension
reject_interpreter_extension = {
	.ext_def = &reject_extension,
	.run = ext_reject_interpreter_run
};

static bool
ext_reject_validator_load(const struct sieve_extension *ext,
			  struct sieve_validator *valdtr)
{
	/* Register new command */
	sieve_validator_register_command(valdtr, ext, &reject_command);

	sieve_validator_extension_register(valdtr, ext,
					   &reject_validator_extension, NULL);
	return TRUE;
}

static bool
ext_reject_interpreter_load(const struct sieve_extension *ext,
			    const struct sieve_runtime_env *renv,
			    sieve_size_t *address ATTR_UNUSED)
{
	sieve_interpreter_extension_register(renv->interp, ext,
					     &reject_interpreter_extension,
					     NULL);
	return TRUE;
}

/* EReject */

static bool
ext_ereject_validator_load(const struct sieve_extension *ext,
			   struct sieve_validator *valdtr);
static bool
ext_ereject_interpreter_load(const struct sieve_extension *ext,
			     const struct sieve_runtime_env *renv,
			     sieve_size_t *address);

const struct sieve_extension_def ereject_extension = {
	.name = "ereject",
	.validator_load = ext_ereject_validator_load,
	.interpreter_load = ext_ereject_interpreter_load,
	SIEVE_EXT_DEFINE_OPERATION(ereject_operation)
};
const struct sieve_validator_extension
ereject_validator_extension = {
	.ext = &ereject_extension,
	.validate = ext_reject_validator_validate
};
const struct sieve_interpreter_extension
ereject_interpreter_extension = {
	.ext_def = &ereject_extension,
	.run = ext_reject_interpreter_run
};

static bool
ext_ereject_validator_load(const struct sieve_extension *ext,
			   struct sieve_validator *valdtr)
{
	/* Register new command */
	sieve_validator_register_command(valdtr, ext, &ereject_command);

	sieve_validator_extension_register(valdtr, ext,
					   &ereject_validator_extension, NULL);
	return TRUE;
}

static bool
ext_ereject_interpreter_load(const struct sieve_extension *ext,
			     const struct sieve_runtime_env *renv,
			     sieve_size_t *address ATTR_UNUSED)
{
	sieve_interpreter_extension_register(renv->interp, ext,
					     &ereject_interpreter_extension,
					     NULL);
	return TRUE;
}

/* Environment checking */

static bool
ext_reject_validator_validate(const struct sieve_extension *ext,
			      struct sieve_validator *valdtr,
			      void *context ATTR_UNUSED,
			      struct sieve_ast_argument *require_arg,
			      bool required)
{
	if (required) {
		enum sieve_compile_flags flags =
			sieve_validator_compile_flags(valdtr);

		if ((flags & SIEVE_COMPILE_FLAG_NO_ENVELOPE) != 0) {
			sieve_argument_validate_error(
				valdtr, require_arg,
				"the %s extension cannot be used in this context "
				"(needs access to message envelope)",
				sieve_extension_name(ext));
			return FALSE;
		}
	}
	return TRUE;
}

static int
ext_reject_interpreter_run(const struct sieve_extension *ext,
			   const struct sieve_runtime_env *renv,
			   void *context ATTR_UNUSED, bool deferred)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	if ((eenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) != 0) {
		if (!deferred) {
			sieve_runtime_error(
				renv, NULL,
				"the %s extension cannot be used in this context "
				"(needs access to message envelope)",
				sieve_extension_name(ext));
		}
		return SIEVE_EXEC_FAILURE;
	}
	return SIEVE_EXEC_OK;
}

/*
 * Commands
 */

/* Forward declarations */

static bool
cmd_reject_validate(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool
cmd_reject_generate(const struct sieve_codegen_env *cgenv,
		    struct sieve_command *cmd);

/* Reject command
 *
 * Syntax:
 *   reject <reason: string>
 */

static const struct sieve_command_def reject_command = {
	.identifier = "reject",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_reject_validate,
	.generate = cmd_reject_generate
};

/* EReject command
 *
 * Syntax:
 *   ereject <reason: string>
 */

static const struct sieve_command_def ereject_command = {
	.identifier = "ereject",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_reject_validate,
	.generate = cmd_reject_generate,
};

/*
 * Operations
 */

/* Forward declarations */

static bool
ext_reject_operation_dump(const struct sieve_dumptime_env *denv,
			  sieve_size_t *address);
static int
ext_reject_operation_execute(const struct sieve_runtime_env *renv,
			     sieve_size_t *address);

/* Reject operation */

static const struct sieve_operation_def reject_operation = {
	.mnemonic = "REJECT",
	.ext_def = &reject_extension,
	.dump = ext_reject_operation_dump,
	.execute = ext_reject_operation_execute
};

/* EReject operation */

static const struct sieve_operation_def ereject_operation = {
	.mnemonic = "EREJECT",
	.ext_def = &ereject_extension,
	.dump = ext_reject_operation_dump,
	.execute = ext_reject_operation_execute
};

/*
 * Reject action
 */

static int
act_reject_check_duplicate(const struct sieve_runtime_env *renv,
			   const struct sieve_action *act,
			   const struct sieve_action *act_other);
int act_reject_check_conflict(const struct sieve_runtime_env *renv,
			      const struct sieve_action *act,
			      const struct sieve_action *act_other);
static void
act_reject_print(const struct sieve_action *action,
		 const struct sieve_result_print_env *rpenv, bool *keep);
static int
act_reject_commit(const struct sieve_action_exec_env *aenv, void *tr_context,
		  bool *keep);

const struct sieve_action_def act_reject = {
	.name = "reject",
	.flags = SIEVE_ACTFLAG_SENDS_RESPONSE,
	.check_duplicate = act_reject_check_duplicate,
	.check_conflict = act_reject_check_conflict,
	.print = act_reject_print,
	.commit = act_reject_commit,
};

struct act_reject_context {
	const char *reason;
	bool ereject;
};

/*
 * Validation
 */

static bool
cmd_reject_validate(struct sieve_validator *valdtr, struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "reason",
						1, SAAT_STRING))
		return FALSE;

	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/*
 * Code generation
 */

static bool
cmd_reject_generate(const struct sieve_codegen_env *cgenv,
		    struct sieve_command *cmd)
{
	if (sieve_command_is(cmd, reject_command)) {
		sieve_operation_emit(cgenv->sblock, cmd->ext,
				     &reject_operation);
	} else {
		sieve_operation_emit(cgenv->sblock, cmd->ext,
				     &ereject_operation);
	}

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, cmd, NULL);
}

/*
 * Code dump
 */

static bool
ext_reject_operation_dump(const struct sieve_dumptime_env *denv,
			  sieve_size_t *address)
{
	sieve_code_dumpf(denv, "%s", sieve_operation_mnemonic(denv->oprtn));
	sieve_code_descend(denv);

	if (sieve_action_opr_optional_dump(denv, address, NULL) != 0)
		return FALSE;

	return sieve_opr_string_dump(denv, address, "reason");
}

/*
 * Interpretation
 */

static int
ext_reject_operation_execute(const struct sieve_runtime_env *renv,
			     sieve_size_t *address)
{
	const struct sieve_operation *oprtn = renv->oprtn;
	const struct sieve_extension *this_ext = oprtn->ext;
	struct sieve_side_effects_list *slist = NULL;
	struct act_reject_context *act;
	string_t *reason;
	pool_t pool;
	int ret;

	/*
	 * Read data
	 */

	/* Optional operands (side effects only) */
	if (sieve_action_opr_optional_read(renv, address, NULL,
					   &ret, &slist) != 0)
		return ret;

	/* Read rejection reason */
	if ((ret = sieve_opr_string_read(renv, address, "reason",
					 &reason)) <= 0)
		return ret;

	/*
	 * Perform operation
	 */

	if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_ACTIONS)) {
		if (sieve_operation_is(oprtn, ereject_operation))
			sieve_runtime_trace(renv, 0, "ereject action");
		else
			sieve_runtime_trace(renv, 0, "reject action");

		sieve_runtime_trace_descend(renv);
		sieve_runtime_trace(renv, 0, "reject message with reason `%s'",
				    str_sanitize(str_c(reason), 64));
	}

	/* Add reject action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_reject_context, 1);
	act->reason = p_strdup(pool, str_c(reason));
	act->ereject = sieve_operation_is(oprtn, ereject_operation);

	if (sieve_result_add_action(renv, this_ext,
				    (act->ereject ? "ereject" : "reject"),
				    &act_reject, slist, (void *)act,
				    0, FALSE) < 0)
		return SIEVE_EXEC_FAILURE;
	return SIEVE_EXEC_OK;
}

/*
 * Action implementation
 */

static int
act_reject_check_duplicate(const struct sieve_runtime_env *renv ATTR_UNUSED,
			   const struct sieve_action *act,
			   const struct sieve_action *act_other)
{
	if (!act_other->executed) {
		sieve_runtime_error(
			renv, act->location,
			"duplicate reject/ereject action not allowed "
			"(previously triggered one was here: %s)",
			act_other->location);
		return -1;
	}

	return 1;
}

int act_reject_check_conflict(const struct sieve_runtime_env *renv,
			      const struct sieve_action *act,
			      const struct sieve_action *act_other)
{
	if ((act_other->def->flags & SIEVE_ACTFLAG_TRIES_DELIVER) > 0) {
		if (!act_other->executed) {
			sieve_runtime_error(
				renv, act->location,
				"reject/ereject action conflicts with other action: "
				"the %s action (%s) tries to deliver the message",
				act_other->def->name, act_other->location);
			return -1;
		}
	}

	if ((act_other->def->flags & SIEVE_ACTFLAG_SENDS_RESPONSE) > 0) {
		struct act_reject_context *rj_ctx;

		if (!act_other->executed) {
			sieve_runtime_error(
				renv, act->location,
				"reject/ereject action conflicts with other action: "
				"the %s action (%s) also sends a response to the sender",
				act_other->def->name, act_other->location);
			return -1;
		}

		/* Conflicting action was already executed, transform reject
		 * into discard equivalent.
		 */
		rj_ctx = (struct act_reject_context *)act->context;
		rj_ctx->reason = NULL;
	}

	return 0;
}

static void
act_reject_print(const struct sieve_action *action,
		 const struct sieve_result_print_env *rpenv, bool *keep)
{
	struct act_reject_context *rj_ctx =
		(struct act_reject_context *)action->context;

	if (rj_ctx->reason != NULL) {
		sieve_result_action_printf(
			rpenv, "reject message with reason: %s",
			str_sanitize(rj_ctx->reason, 128));
	} else {
		sieve_result_action_printf(
			rpenv,
			"reject message without sending a response (discard)");
	}

	*keep = FALSE;
}

static int
act_reject_commit(const struct sieve_action_exec_env *aenv,
		  void *tr_context ATTR_UNUSED, bool *keep)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct act_reject_context *rj_ctx =
		(struct act_reject_context *)aenv->action->context;
	const struct smtp_address *sender, *recipient;
	int ret;

	sender = sieve_message_get_sender(aenv->msgctx);
	recipient = sieve_message_get_orig_recipient(aenv->msgctx);

	if ((eenv->flags & SIEVE_EXECUTE_FLAG_SKIP_RESPONSES) != 0) {
		sieve_result_global_log(
			aenv, "not sending reject message (skipped)");
		return SIEVE_EXEC_OK;
	}
	if (smtp_address_isnull(recipient)) {
		sieve_result_global_warning(
			aenv, "reject action aborted: envelope recipient is <>");
		return SIEVE_EXEC_OK;
	}
	if (rj_ctx->reason == NULL) {
		sieve_result_global_log(
			aenv, "not sending reject message "
			"(would cause second response to sender)");
		*keep = FALSE;
		return SIEVE_EXEC_OK;
	}
	if (smtp_address_isnull(sender)) {
		sieve_result_global_log(
			aenv, "not sending reject message to <>");
		*keep = FALSE;
		return SIEVE_EXEC_OK;
	}

	if ((ret = sieve_action_reject_mail(aenv, recipient,
					    rj_ctx->reason)) <= 0)
		return ret;

	eenv->exec_status->significant_action_executed = TRUE;

	struct event_passthrough *e = sieve_action_create_finish_event(aenv);

	sieve_result_event_log(aenv, e->event(),
			       "rejected message from <%s> (%s)",
			       smtp_address_encode(sender),
			       (rj_ctx->ereject ? "ereject" : "reject"));

	*keep = FALSE;
	return SIEVE_EXEC_OK;
}
