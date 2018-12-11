/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "buffer.h"
#include "str.h"
#include "str-sanitize.h"
#include "istream.h"
#include "ostream.h"
#include "safe-mkstemp.h"
#include "mail-user.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-binary.h"
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

#include "sieve-ext-variables.h"

#include "sieve-extprograms-common.h"

/* Filter command
 *
 * Syntax:
 *   "filter" <program-name: string> [<arguments: string-list>]
 *
 */

static bool cmd_filter_generate
	(const struct sieve_codegen_env *cgenv, 
		struct sieve_command *ctx);

const struct sieve_command_def sieve_cmd_filter = {
	.identifier = "filter",
	.type = SCT_HYBRID,
	.positional_args = -1, /* We check positional arguments ourselves */
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = sieve_extprogram_command_validate,
	.generate = cmd_filter_generate
};

/* 
 * Filter operation 
 */

static bool cmd_filter_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_filter_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def sieve_opr_filter = {
	.mnemonic = "FILTER",
	.ext_def = &sieve_ext_vnd_filter,
	.dump = cmd_filter_operation_dump, 
	.execute = cmd_filter_operation_execute
};

/*
 * Code generation
 */

static bool cmd_filter_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &sieve_opr_filter);

	/* Emit is_test flag */
	sieve_binary_emit_byte(cgenv->sblock,
		(uint8_t)( cmd->ast_node->type == SAT_TEST ? 1 : 0 ));

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
 
static bool cmd_filter_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{	
	unsigned int is_test = 0;

	/* Read is_test flag */
	if ( !sieve_binary_read_byte(denv->sblock, address, &is_test) )
		return FALSE;

	sieve_code_dumpf(denv, "FILTER (%s)",
		(is_test > 0 ? "test" : "command"));
	sieve_code_descend(denv);		

	/* Dump optional operands */
	if ( sieve_action_opr_optional_dump(denv, address, NULL) != 0 )
		return FALSE;

	if ( !sieve_opr_string_dump(denv, address, "program-name") )
		return FALSE;

	return sieve_opr_stringlist_dump_ex(denv, address, "arguments", "");
}

/* 
 * Code execution
 */

static int cmd_filter_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	unsigned int is_test = 0;
	struct sieve_stringlist *args_list = NULL;
	enum sieve_error error = SIEVE_ERROR_NONE;
	string_t *pname = NULL;
	const char *program_name = NULL;
	const char *const *args = NULL;
	struct istream *newmsg = NULL;
	struct sieve_extprogram *sprog;
	int ret;

	/*
	 * Read operands
	 */

	/* The is_test flag */

	if ( !sieve_binary_read_byte(renv->sblock, address, &is_test) ) {
		sieve_runtime_trace_error(renv, "invalid is_test flag");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Optional operands */	

	if ( sieve_action_opr_optional_read(renv, address, NULL, &ret, NULL) != 0 ) 
		return ret;

	/* Fixed operands */

	if ( (ret=sieve_extprogram_command_read_operands
		(renv, address, &pname, &args_list)) <= 0 )
		return ret;

	program_name = str_c(pname);
	if ( args_list != NULL &&
		sieve_stringlist_read_all(args_list, pool_datastack_create(), &args) < 0 ) {
		sieve_runtime_trace_error(renv, "failed to read args operand");
		return args_list->exec_status;
	}

	/*
	 * Perform operation
	 */

	/* Trace */

	sieve_runtime_trace(renv, SIEVE_TRLVL_ACTIONS, "filter action");
	sieve_runtime_trace_descend(renv);
	sieve_runtime_trace(renv, SIEVE_TRLVL_ACTIONS,
		"execute program `%s'", str_sanitize(program_name, 128));

	sprog = sieve_extprogram_create
		(this_ext, renv->scriptenv, renv->msgdata, "filter", program_name, args,
			&error);

	if ( sprog != NULL ) {
		struct mail *mail = sieve_message_get_mail(renv->msgctx);

		if ( sieve_extprogram_set_input_mail(sprog, mail) < 0 ) {
			sieve_extprogram_destroy(&sprog);
			return sieve_runtime_mail_error(renv, mail,
				"filter action: failed to read input message");
		}
		sieve_extprogram_set_output_seekable(sprog);
		ret = sieve_extprogram_run(sprog);
	} else {
		ret = -1;
	}

	if ( ret > 0 )
		newmsg = sieve_extprogram_get_output_seekable(sprog);
	if ( sprog != NULL )
		sieve_extprogram_destroy(&sprog);

	if ( ret > 0 && newmsg != NULL ) {
		sieve_runtime_trace(renv,	SIEVE_TRLVL_ACTIONS,
			"executed program successfully");

		i_stream_set_name(newmsg,
			t_strdup_printf("filter %s output", program_name));
		newmsg->blocking = TRUE;
		if ( (ret=sieve_message_substitute(renv->msgctx, newmsg)) >= 0 ) {
			sieve_runtime_trace(renv,	SIEVE_TRLVL_ACTIONS,
				"changed message");
		} else {
			sieve_runtime_critical(renv, NULL, "filter action",
				"filter action: failed to substitute message"); 
		}

		i_stream_unref(&newmsg);

	} else if ( ret < 0 ) {
		if ( error == SIEVE_ERROR_NOT_FOUND ) {
			sieve_runtime_error(renv, NULL,
				"filter action: program `%s' not found",
				str_sanitize(program_name, 80));
		} else {
			sieve_extprogram_exec_error(renv->ehandler,
				sieve_runtime_get_full_command_location(renv),
				"filter action: failed to execute to program `%s'",
				str_sanitize(program_name, 80));
		}

	} else {
		sieve_runtime_trace(renv,	SIEVE_TRLVL_ACTIONS,
			"filter action: program indicated false result");
	}

	if ( is_test > 0 ) {
		sieve_interpreter_set_test_result(renv->interp, ( ret > 0 ));

		return SIEVE_EXEC_OK;
	}

	return ( ret >= 0 ? SIEVE_EXEC_OK : SIEVE_EXEC_FAILURE );
}

