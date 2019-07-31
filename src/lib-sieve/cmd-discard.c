/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-dump.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

/*
 * Discard command
 *
 * Syntax
 *   discard
 */

static bool
cmd_discard_generate(const struct sieve_codegen_env *cgenv,
		     struct sieve_command *ctx ATTR_UNUSED);

const struct sieve_command_def cmd_discard = {
	.identifier = "discard",
	.type = SCT_COMMAND,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.generate = cmd_discard_generate
};

/*
 * Discard operation
 */

static bool
cmd_discard_operation_dump(const struct sieve_dumptime_env *denv,
			   sieve_size_t *address);
static int
cmd_discard_operation_execute(const struct sieve_runtime_env *renv,
			      sieve_size_t *address);

const struct sieve_operation_def cmd_discard_operation = {
	.mnemonic = "DISCARD",
	.code = SIEVE_OPERATION_DISCARD,
	.dump = cmd_discard_operation_dump,
	.execute = cmd_discard_operation_execute
};

/*
 * Discard actions
 */

static bool
act_discard_equals(const struct sieve_script_env *senv,
		   const struct sieve_action *act1,
		   const struct sieve_action *act2);
static int
act_discard_check_duplicate(const struct sieve_runtime_env *renv,
			    const struct sieve_action *act,
			    const struct sieve_action *act_other);
static void
act_discard_print(const struct sieve_action *action,
		  const struct sieve_result_print_env *rpenv, bool *keep);
static int
act_discard_commit(const struct sieve_action_exec_env *aenv, void *tr_context,
		   bool *keep);

const struct sieve_action_def act_discard = {
	.name = "discard",
	.equals = act_discard_equals,
	.check_duplicate = act_discard_check_duplicate,
	.print = act_discard_print,
	.commit = act_discard_commit,
};

/*
 * Code generation
 */

static bool
cmd_discard_generate(const struct sieve_codegen_env *cgenv,
		     struct sieve_command *cmd ATTR_UNUSED)
{
	sieve_operation_emit(cgenv->sblock, NULL, &cmd_discard_operation);

	return TRUE;
}

/*
 * Code dump
 */

static bool
cmd_discard_operation_dump(const struct sieve_dumptime_env *denv,
			   sieve_size_t *address)
{
	sieve_code_dumpf(denv, "DISCARD");
	sieve_code_descend(denv);

	return (sieve_action_opr_optional_dump(denv, address, NULL) == 0);
}

/*
 * Interpretation
 */

static int
cmd_discard_operation_execute(const struct sieve_runtime_env *renv ATTR_UNUSED,
			      sieve_size_t *address ATTR_UNUSED)
{
	sieve_runtime_trace(renv, SIEVE_TRLVL_ACTIONS,
		"discard action; cancel implicit keep");

	if (sieve_result_add_action(renv, NULL, "discard", &act_discard,
				    NULL, NULL, 0, FALSE) < 0)
		return SIEVE_EXEC_FAILURE;
	return SIEVE_EXEC_OK;
}

/*
 * Action implementation
 */

static bool
act_discard_equals(const struct sieve_script_env *senv ATTR_UNUSED,
		   const struct sieve_action *act1 ATTR_UNUSED,
		   const struct sieve_action *act2 ATTR_UNUSED)
{
	return TRUE;
}

static int
act_discard_check_duplicate(const struct sieve_runtime_env *renv ATTR_UNUSED,
			    const struct sieve_action *act ATTR_UNUSED,
			    const struct sieve_action *act_other ATTR_UNUSED)
{
	return 1;
}

static void
act_discard_print(const struct sieve_action *action ATTR_UNUSED,
		  const struct sieve_result_print_env *rpenv, bool *keep)
{
	sieve_result_action_printf(rpenv, "discard");

	*keep = FALSE;
}

static int
act_discard_commit(const struct sieve_action_exec_env *aenv,
		   void *tr_context ATTR_UNUSED, bool *keep)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;

	eenv->exec_status->significant_action_executed = TRUE;

	struct event_passthrough *e = sieve_action_create_finish_event(aenv);

	sieve_result_event_log(aenv, e->event(),
		"Marked message to be discarded if not explicitly delivered "
		"(discard action)");
	*keep = FALSE;

	return SIEVE_EXEC_OK;
}

