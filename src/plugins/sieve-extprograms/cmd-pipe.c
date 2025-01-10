/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "str.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"

#include "sieve-extprograms-common.h"

/* Pipe command
 *
 * Syntax:
 *   pipe [":copy"] [":try"] <program-name: string> [<arguments: string-list>]
 *
 */

static bool
cmd_pipe_registered(struct sieve_validator *valdtr,
		    const struct sieve_extension *ext,
		    struct sieve_command_registration *cmd_reg);
static bool
cmd_pipe_generate(const struct sieve_codegen_env *cgenv,
		  struct sieve_command *ctx);

const struct sieve_command_def sieve_cmd_pipe = {
	.identifier = "pipe",
	.type = SCT_COMMAND,
	.positional_args = -1, /* We check positional arguments ourselves */
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = cmd_pipe_registered,
	.validate = sieve_extprogram_command_validate,
	.generate = cmd_pipe_generate,
};

/*
 * Tagged arguments
 */

static const struct sieve_argument_def pipe_try_tag = {
	.identifier = "try",
};

/*
 * Pipe operation
 */

static bool
cmd_pipe_operation_dump(const struct sieve_dumptime_env *denv,
			sieve_size_t *address);
static int
cmd_pipe_operation_execute(const struct sieve_runtime_env *renv,
			   sieve_size_t *address);

const struct sieve_operation_def sieve_opr_pipe = {
	.mnemonic = "PIPE",
	.ext_def = &sieve_ext_vnd_pipe,
	.dump = cmd_pipe_operation_dump,
	.execute = cmd_pipe_operation_execute,
};

/* Codes for optional operands */

enum cmd_pipe_optional {
	OPT_END,
	OPT_TRY,
};

/*
 * Pipe action
 */

/* Forward declarations */

static int
act_pipe_check_duplicate(const struct sieve_runtime_env *renv,
			 const struct sieve_action *act,
			 const struct sieve_action *act_other);
static void
act_pipe_print(const struct sieve_action *action,
	       const struct sieve_result_print_env *rpenv,
	       bool *keep);
static int
act_pipe_start(const struct sieve_action_exec_env *aenv, void **tr_context);
static int
act_pipe_execute(const struct sieve_action_exec_env *aenv,
		void *tr_context, bool *keep);
static int
act_pipe_commit(const struct sieve_action_exec_env *aenv,
		void *tr_context);
static void
act_pipe_rollback(const struct sieve_action_exec_env *aenv,
		  void *tr_context, bool success);

/* Action object */

const struct sieve_action_def act_pipe = {
	.name = "pipe",
	.flags = SIEVE_ACTFLAG_TRIES_DELIVER,
	.check_duplicate = act_pipe_check_duplicate,
	.print = act_pipe_print,
	.start = act_pipe_start,
	.execute = act_pipe_execute,
	.commit = act_pipe_commit,
	.rollback = act_pipe_rollback,
};

/* Action context information */

struct ext_pipe_action {
	const char *program_name;
	const char *const *args;
	bool try;
};

/*
 * Command registration
 */

static bool
cmd_pipe_registered(struct sieve_validator *valdtr,
		    const struct sieve_extension *ext,
		    struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &pipe_try_tag, OPT_TRY);
	return TRUE;
}

/*
 * Code generation
 */

static bool
cmd_pipe_generate(const struct sieve_codegen_env *cgenv,
		  struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &sieve_opr_pipe);

	/* Generate arguments */
	if (!sieve_generate_arguments(cgenv, cmd, NULL))
		return FALSE;

	/* Emit a placeholder when the <arguments> argument is missing */
	if (sieve_ast_argument_next(cmd->first_positional) == NULL)
		sieve_opr_omitted_emit(cgenv->sblock);
	return TRUE;
}

/*
 * Code dump
 */

static bool
cmd_pipe_operation_dump(const struct sieve_dumptime_env *denv,
			sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "PIPE");
	sieve_code_descend(denv);

	/* Dump optional operands */
	for (;;) {
		int opt;

		opt = sieve_action_opr_optional_dump(denv, address, &opt_code);
		if (opt < 0)
			return FALSE;
		if (opt == 0)
			break;

		switch (opt_code) {
		case OPT_TRY:
			sieve_code_dumpf(denv, "try");
			break;
		default:
			return FALSE;
		}
	}

	if (!sieve_opr_string_dump(denv, address, "program-name"))
		return FALSE;

	return sieve_opr_stringlist_dump_ex(denv, address, "arguments", "");
}

/*
 * Code execution
 */

static int
cmd_pipe_operation_execute(const struct sieve_runtime_env *renv,
			   sieve_size_t *address)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct sieve_side_effects_list *slist = NULL;
	struct ext_pipe_action *act;
	pool_t pool;
	int opt_code = 0;
	struct sieve_stringlist *args_list = NULL;
	string_t *pname = NULL;
	bool try = FALSE;
	int ret;

	/*
	 * Read operands
	 */

	/* Optional operands */

	for (;;) {
		int opt;

		opt = sieve_action_opr_optional_read(renv, address,
						     &opt_code, &ret, &slist);
		if (ret < 0)
			return ret;
		if (opt == 0)
			break;

		switch (opt_code) {
		case OPT_TRY:
			try = TRUE;
			break;
		default:
			sieve_runtime_trace_error(
				renv, "unknown optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	}

	/* Fixed operands */

	ret = sieve_extprogram_command_read_operands(renv, address,
						     &pname, &args_list);
	if (ret <= 0)
		return ret;

	/*
	 * Perform operation
	 */

	/* Trace */

	sieve_runtime_trace(renv, SIEVE_TRLVL_ACTIONS, "pipe action");

	/* Compose action */

	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct ext_pipe_action, 1);

	if (args_list != NULL &&
	    sieve_stringlist_read_all(args_list, pool, &act->args) < 0) {
		sieve_runtime_trace_error(renv, "failed to read args operand");
		return args_list->exec_status;
	}

	act->program_name = p_strdup(pool, str_c(pname));
	act->try = try;

	if (sieve_result_add_action(renv, this_ext, "pipe", &act_pipe, slist,
				    (void *)act, 0, TRUE) < 0)
		return SIEVE_EXEC_FAILURE;
	return SIEVE_EXEC_OK;
}

/*
 * Action
 */

/* Runtime verification */

static int
act_pipe_check_duplicate(const struct sieve_runtime_env *renv ATTR_UNUSED,
			 const struct sieve_action *act,
			 const struct sieve_action *act_other)
{
	struct ext_pipe_action *new_act, *old_act;

	if (act->context == NULL || act_other->context == NULL)
		return 0;

	new_act = (struct ext_pipe_action *) act->context;
	old_act = (struct ext_pipe_action *) act_other->context;

	if (strcmp(new_act->program_name, old_act->program_name) == 0) {
		sieve_runtime_error(renv, act->location,
				    "duplicate pipe \"%s\" action not allowed "
				    "(previously triggered one was here: %s)",
				    new_act->program_name, act_other->location);
		return -1;
	}

	return 0;
}

/* Result printing */

static void
act_pipe_print(const struct sieve_action *action,
	       const struct sieve_result_print_env *rpenv,
	       bool *keep ATTR_UNUSED)
{
	const struct ext_pipe_action *act =
		(const struct ext_pipe_action *)action->context;

	sieve_result_action_printf(
		rpenv, "pipe message to external program '%s':",
		act->program_name);

	/* Print main method parameters */

	sieve_result_printf(
		rpenv, "    => try           : %s\n",
		(act->try ? "yes" : "no"));

	/* FIXME: print args */

	/* Finish output with an empty line */
	sieve_result_printf(rpenv, "\n");
}

/* Result execution */

struct act_pipe_transaction {
	struct sieve_extprogram *sprog;
};

static int
act_pipe_start(const struct sieve_action_exec_env *aenv, void **tr_context)
{
	struct act_pipe_transaction *trans;
	pool_t pool = sieve_result_pool(aenv->result);

	/* Create transaction context */
	trans = p_new(pool, struct act_pipe_transaction, 1);
	*tr_context = (void *)trans;

	return SIEVE_EXEC_OK;
}

static int
act_pipe_execute(const struct sieve_action_exec_env *aenv,
		 void *tr_context, bool *keep)
{
	const struct sieve_action *action = aenv->action;
	const struct sieve_execute_env *eenv = aenv->exec_env;
	const struct ext_pipe_action *act =
		(const struct ext_pipe_action *)action->context;
	struct act_pipe_transaction *trans = tr_context;
	struct mail *mail = (action->mail != NULL ?
		     action->mail :
		     sieve_message_get_mail(aenv->msgctx));
	enum sieve_error error = SIEVE_ERROR_NONE;

	trans->sprog = sieve_extprogram_create(action->ext, eenv->scriptenv,
					       eenv->msgdata, "pipe",
					       act->program_name, act->args,
					       &error);
	if (trans->sprog != NULL) {
		if (sieve_extprogram_set_input_mail(trans->sprog, mail) < 0) {
			sieve_extprogram_destroy(&trans->sprog);
			return sieve_result_mail_error(
				aenv, mail, "failed to read input message");
		}
	}

	*keep = FALSE;
	return SIEVE_EXEC_OK;
}

static int
act_pipe_commit(const struct sieve_action_exec_env *aenv,
		void *tr_context ATTR_UNUSED)
{
	const struct sieve_action *action = aenv->action;
	const struct sieve_execute_env *eenv = aenv->exec_env;
	const struct ext_pipe_action *act =
		(const struct ext_pipe_action *)action->context;
	struct act_pipe_transaction *trans = tr_context;
	enum sieve_error error = SIEVE_ERROR_NONE;
	int ret;

	if (trans->sprog != NULL) {
		ret = sieve_extprogram_run(trans->sprog);
		sieve_extprogram_destroy(&trans->sprog);
	} else {
		ret = -1;
	}

	if (ret > 0) {
		struct event_passthrough *e =
			sieve_action_create_finish_event(aenv)->
			add_str("pipe_program",
				str_sanitize(act->program_name, 256));

		sieve_result_event_log(aenv, e->event(),
				       "piped message to program `%s'",
				       str_sanitize(act->program_name, 128));

		/* Indicate that message was successfully 'forwarded' */
		eenv->exec_status->message_forwarded = TRUE;
	} else {
		if (ret < 0) {
			if (error == SIEVE_ERROR_NOT_FOUND) {
				sieve_result_error(
					aenv,
					"failed to pipe message to program: "
					"program `%s' not found",
					str_sanitize(act->program_name, 80));
			} else {
				sieve_extprogram_exec_error(
					aenv->ehandler, NULL,
					"failed to pipe message to program `%s'",
					str_sanitize(act->program_name, 80));
			}
		} else {
			sieve_extprogram_exec_error(
				aenv->ehandler, NULL,
				"failed to execute to program `%s'",
				str_sanitize(act->program_name, 80));
		}

		if (act->try)
			return SIEVE_EXEC_OK;
		return SIEVE_EXEC_FAILURE;
	}

	return SIEVE_EXEC_OK;
}

static void
act_pipe_rollback(const struct sieve_action_exec_env *aenv ATTR_UNUSED,
		  void *tr_context, bool success ATTR_UNUSED)
{
	struct act_pipe_transaction *trans = tr_context;

	if (trans->sprog != NULL)
		sieve_extprogram_destroy(&trans->sprog);
}
