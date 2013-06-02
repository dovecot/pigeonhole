/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
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

static bool cmd_pipe_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool cmd_pipe_generate
	(const struct sieve_codegen_env *cgenv, 
		struct sieve_command *ctx);

const struct sieve_command_def cmd_pipe = {
	"pipe",
	SCT_COMMAND,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE,
	cmd_pipe_registered,
	NULL,
	sieve_extprogram_command_validate,
	NULL,
	cmd_pipe_generate,
	NULL,
};

/*
 * Tagged arguments
 */

static const struct sieve_argument_def pipe_try_tag = { 
	"try", 
	NULL, NULL, NULL, NULL, NULL 
};

/* 
 * Pipe operation 
 */

static bool cmd_pipe_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_pipe_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def cmd_pipe_operation = { 
	"PIPE", &pipe_extension, 0,
	cmd_pipe_operation_dump, 
	cmd_pipe_operation_execute
};

/* Codes for optional operands */

enum cmd_pipe_optional {
  OPT_END,
  OPT_TRY
};

/* 
 * Pipe action 
 */

/* Forward declarations */

static int act_pipe_check_duplicate
	(const struct sieve_runtime_env *renv, 
		const struct sieve_action *act,
		const struct sieve_action *act_other);
static void act_pipe_print
	(const struct sieve_action *action,
		const struct sieve_result_print_env *rpenv, bool *keep);	
static int act_pipe_commit
	(const struct sieve_action *action,	
		const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep);

/* Action object */

const struct sieve_action_def act_pipe = {
	"pipe",
	SIEVE_ACTFLAG_TRIES_DELIVER,
	NULL,
	act_pipe_check_duplicate, 
	NULL,
	act_pipe_print,
	NULL, NULL,
	act_pipe_commit,
	NULL
};

/* Action context information */
		
struct ext_pipe_action {
	const char *program_name;
	const char * const *args;
	bool try;
};

/*
 * Command registration
 */

static bool cmd_pipe_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &pipe_try_tag, OPT_TRY);

	return TRUE;
}

/*
 * Code generation
 */

static bool cmd_pipe_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &cmd_pipe_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;

	/* Emit a placeholder when the <arguments> argument is missing */
	if ( sieve_ast_argument_next(cmd->first_positional) == NULL )
		sieve_opr_omitted_emit(cgenv->sblock);

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_pipe_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{	
	int opt_code = 0;
	
	sieve_code_dumpf(denv, "PIPE");
	sieve_code_descend(denv);	

	/* Dump optional operands */
	for (;;) {
		int opt;
		bool opok = TRUE;

		if ( (opt=sieve_action_opr_optional_dump(denv, address, &opt_code)) < 0 )
			return FALSE;

		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case OPT_TRY:
			sieve_code_dumpf(denv, "try");	
			break;
		default:
			return FALSE;
		}

		if ( !opok ) return FALSE;
	}
	
	if ( !sieve_opr_string_dump(denv, address, "program-name") )
		return FALSE;

	return sieve_opr_stringlist_dump_ex(denv, address, "arguments", "");
}

/* 
 * Code execution
 */

static int cmd_pipe_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
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

		if ( (opt=sieve_action_opr_optional_read
			(renv, address, &opt_code, &ret, &slist)) < 0 )
			return ret;

		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case OPT_TRY:
			try = TRUE;
			ret = SIEVE_EXEC_OK;
			break;
		default:
			sieve_runtime_trace_error(renv, "unknown optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}

		if ( ret <= 0 ) return ret;
	}

	/* Fixed operands */

	if ( (ret=sieve_extprogram_command_read_operands
		(renv, address, &pname, &args_list)) <= 0 )
		return ret;

	/*
	 * Perform operation
	 */

	/* Trace */

	sieve_runtime_trace(renv, SIEVE_TRLVL_ACTIONS, "pipe action");	

	/* Compose action */

	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct ext_pipe_action, 1);

	if ( args_list != NULL &&
		sieve_stringlist_read_all(args_list, pool, &act->args) < 0 ) {
		sieve_runtime_trace_error(renv, "failed to read args operand");
		return args_list->exec_status;
	}
	
	act->program_name = p_strdup(pool, str_c(pname));
	act->try = try;

	if ( sieve_result_add_action
		(renv, this_ext, &act_pipe, slist, (void *) act, 0, TRUE) < 0 ) {
		return SIEVE_EXEC_FAILURE;
	}

	return SIEVE_EXEC_OK;
}

/*
 * Action
 */

/* Runtime verification */

static int act_pipe_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED, 
	const struct sieve_action *act,
	const struct sieve_action *act_other)
{
	struct ext_pipe_action *new_act, *old_act;
		
	if ( act->context == NULL || act_other->context == NULL )
		return 0;

	new_act = (struct ext_pipe_action *) act->context;
	old_act = (struct ext_pipe_action *) act_other->context;

	if ( strcmp(new_act->program_name, old_act->program_name) == 0 ) {
		sieve_runtime_error(renv, act->location,
			"duplicate pipe \"%s\" action not allowed "
			"(previously triggered one was here: %s)",
			new_act->program_name, act_other->location);
		return -1;
	}

	return 0;
}

/* Result printing */
 
static void act_pipe_print
(const struct sieve_action *action,	const struct sieve_result_print_env *rpenv, 
	bool *keep ATTR_UNUSED)	
{
	const struct ext_pipe_action *act = 
		(const struct ext_pipe_action *) action->context;

	sieve_result_action_printf
		( rpenv, "pipe message to external program '%s':", act->program_name);
	
	/* Print main method parameters */

	sieve_result_printf
		( rpenv, "    => try           : %s\n", (act->try ? "yes" : "no") );

	/* FIXME: print args */

	/* Finish output with an empty line */

	sieve_result_printf(rpenv, "\n");
}

/* Result execution */

static int act_pipe_commit
(const struct sieve_action *action, const struct sieve_action_exec_env *aenv, 
	void *tr_context ATTR_UNUSED, bool *keep)
{
	const struct ext_pipe_action *act = 
		(const struct ext_pipe_action *) action->context;
	enum sieve_error error = SIEVE_ERROR_NONE;
	struct mail *mail =	( action->mail != NULL ?
		action->mail : sieve_message_get_mail(aenv->msgctx) );
	struct sieve_extprogram *sprog;
	int ret;

	sprog = sieve_extprogram_create
		(action->ext, aenv->scriptenv, aenv->msgdata, "pipe",
			act->program_name, act->args, &error);
	if ( sprog != NULL && sieve_extprogram_set_input_mail(sprog, mail) >= 0 ) {
		ret = sieve_extprogram_run(sprog);
	} else {
		ret = -1;
	}
	if ( sprog != NULL )
		sieve_extprogram_destroy(&sprog);

	if ( ret > 0 ) {
		sieve_result_global_log(aenv, "pipe action: "
			"piped message to program `%s'", str_sanitize(act->program_name, 128));

		/* Indicate that message was successfully 'forwarded' */
		aenv->exec_status->message_forwarded = TRUE;
	} else {
		if ( ret < 0 ) {
			if ( error == SIEVE_ERROR_NOT_FOUND ) {
				sieve_result_error(aenv, "pipe action: "
					"failed to pipe message to program: program `%s' not found",
					str_sanitize(act->program_name, 80));						
			} else {
				sieve_extprogram_exec_error(aenv->ehandler, NULL,
					"pipe action: failed to pipe message to program `%s'",
					str_sanitize(act->program_name, 80));
			}
		} else {
			sieve_extprogram_exec_error(aenv->ehandler, NULL,
				"pipe action: failed to execute to program `%s'",
				str_sanitize(act->program_name, 80));
		}

		if ( act->try ) return SIEVE_EXEC_OK;

		return SIEVE_EXEC_FAILURE;
	}

	*keep = FALSE;
	return SIEVE_EXEC_OK;
}







