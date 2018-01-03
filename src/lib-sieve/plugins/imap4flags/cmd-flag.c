/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-code.h"
#include "sieve-stringlist.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-imap4flags-common.h"

/*
 * Commands
 */

/* Forward declarations */

static bool cmd_flag_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

/* Setflag command
 *
 * Syntax:
 *   setflag [<variablename: string>] <list-of-flags: string-list>
 */

const struct sieve_command_def cmd_setflag = {
	.identifier = "setflag",
	.type = SCT_COMMAND,
	.positional_args = -1, /* We check positional arguments ourselves */
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = ext_imap4flags_command_validate,
	.generate = cmd_flag_generate
};

/* Addflag command
 *
 * Syntax:
 *   addflag [<variablename: string>] <list-of-flags: string-list>
 */

const struct sieve_command_def cmd_addflag = {
	.identifier = "addflag",
	.type = SCT_COMMAND,
	.positional_args = -1, /* We check positional arguments ourselves */
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = ext_imap4flags_command_validate,
	.generate = cmd_flag_generate
};


/* Removeflag command
 *
 * Syntax:
 *   removeflag [<variablename: string>] <list-of-flags: string-list>
 */

const struct sieve_command_def cmd_removeflag = {
	.identifier = "removeflag",
	.type = SCT_COMMAND,
	.positional_args = -1, /* We check positional arguments ourselves */
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = ext_imap4flags_command_validate,
	.generate = cmd_flag_generate
};

/*
 * Operations
 */

/* Forward declarations */

bool cmd_flag_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_flag_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Setflag operation */

const struct sieve_operation_def setflag_operation = {
	.mnemonic = "SETFLAG",
	.ext_def = &imap4flags_extension,
	.code = EXT_IMAP4FLAGS_OPERATION_SETFLAG,
	.dump = cmd_flag_operation_dump,
	.execute = cmd_flag_operation_execute
};

/* Addflag operation */

const struct sieve_operation_def addflag_operation = {
	.mnemonic = "ADDFLAG",
	.ext_def = &imap4flags_extension,
	.code = EXT_IMAP4FLAGS_OPERATION_ADDFLAG,
	.dump = cmd_flag_operation_dump,
	.execute = cmd_flag_operation_execute
};

/* Removeflag operation */

const struct sieve_operation_def removeflag_operation = {
	.mnemonic = "REMOVEFLAG",
	.ext_def = &imap4flags_extension,
	.code = EXT_IMAP4FLAGS_OPERATION_REMOVEFLAG,
	.dump = cmd_flag_operation_dump,
	.execute = cmd_flag_operation_execute
};

/*
 * Code generation
 */

static bool cmd_flag_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg1, *arg2;

	/* Emit operation */
	if ( sieve_command_is(cmd, cmd_setflag) )
		sieve_operation_emit(cgenv->sblock, cmd->ext, &setflag_operation);
	else if ( sieve_command_is(cmd, cmd_addflag) )
		sieve_operation_emit(cgenv->sblock, cmd->ext, &addflag_operation);
	else if ( sieve_command_is(cmd, cmd_removeflag) )
		sieve_operation_emit(cgenv->sblock, cmd->ext, &removeflag_operation);

	arg1 = cmd->first_positional;
	arg2 = sieve_ast_argument_next(arg1);

	if ( arg2 == NULL ) {
		/* No variable */
		sieve_opr_omitted_emit(cgenv->sblock);
		if ( !sieve_generate_argument(cgenv, arg1, cmd) )
			return FALSE;
	} else {
		/* Full command */
		if ( !sieve_generate_argument(cgenv, arg1, cmd) )
			return FALSE;
		if ( !sieve_generate_argument(cgenv, arg2, cmd) )
			return FALSE;
	}
	return TRUE;
}

/*
 * Code dump
 */

bool cmd_flag_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	struct sieve_operand oprnd;

	sieve_code_dumpf(denv, "%s", sieve_operation_mnemonic(denv->oprtn));
	sieve_code_descend(denv);

	sieve_code_mark(denv);
	if ( !sieve_operand_read(denv->sblock, address, NULL, &oprnd) ) {
		sieve_code_dumpf(denv, "ERROR: INVALID OPERAND");
		return FALSE;
	}

	if ( !sieve_operand_is_omitted(&oprnd) ) {
		return
			sieve_opr_string_dump_data(denv, &oprnd, address, "variable name") &&
			sieve_opr_stringlist_dump(denv, address, "list of flags");
	}

	return
		sieve_opr_stringlist_dump(denv, address, "list of flags");
}

/*
 * Code execution
 */

static int cmd_flag_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_operation *op = renv->oprtn;
	struct sieve_operand oprnd;
	struct sieve_stringlist *flag_list;
	struct sieve_variable_storage *storage;
	unsigned int var_index;
	ext_imapflag_flag_operation_t flag_op;
	int ret;

	/*
	 * Read operands
	 */

	/* Read bare operand (two types possible) */
	if ( (ret=sieve_operand_runtime_read
		(renv, address, NULL, &oprnd)) <= 0 )
		return ret;

	/* Variable operand (optional) */
	if ( !sieve_operand_is_omitted(&oprnd) ) {
		/* Read the variable operand */
		if ( (ret=sieve_variable_operand_read_data
			(renv, &oprnd, address, "variable", &storage, &var_index)) <= 0 )
			return ret;

		/* Read flag list */
		if ( (ret=sieve_opr_stringlist_read(renv, address, "flag-list", &flag_list))
			<= 0 )
			return ret;

	/* Flag-list operand */
	} else {
		storage = NULL;
		var_index = 0;

		/* Read flag list */
		if ( (ret=sieve_opr_stringlist_read(renv, address,
			"flag-list", &flag_list)) <= 0 )
			return ret;
	}

	/*
	 * Perform operation
	 */

	/* Determine what to do */

	if ( sieve_operation_is(op, setflag_operation) ) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS, "setflag command");
		flag_op = sieve_ext_imap4flags_set_flags;
	} else if ( sieve_operation_is(op, addflag_operation) ) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS, "addflag command");
		flag_op = sieve_ext_imap4flags_add_flags;
	} else if ( sieve_operation_is(op, removeflag_operation) ) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS, "removeflag command");
		flag_op = sieve_ext_imap4flags_remove_flags;
	} else {
		i_unreached();
	}

	sieve_runtime_trace_descend(renv);

	/* Perform requested operation */
	return flag_op(renv, op->ext, storage, var_index, flag_list);
}
