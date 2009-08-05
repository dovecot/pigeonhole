/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-code.h"
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
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

/* Setflag command 
 *
 * Syntax: 
 *   setflag [<variablename: string>] <list-of-flags: string-list>
 */
 
const struct sieve_command cmd_setflag = { 
	"setflag", 
	SCT_COMMAND,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE, 
	NULL, NULL,
	ext_imap4flags_command_validate, 
	cmd_flag_generate, 
	NULL 
};

/* Addflag command 
 *
 * Syntax:
 *   addflag [<variablename: string>] <list-of-flags: string-list>
 */

const struct sieve_command cmd_addflag = { 
	"addflag", 
	SCT_COMMAND,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE, 
	NULL, NULL,
	ext_imap4flags_command_validate, 
	cmd_flag_generate, 
	NULL 
};


/* Removeflag command 
 *
 * Syntax:
 *   removeflag [<variablename: string>] <list-of-flags: string-list>
 */

const struct sieve_command cmd_removeflag = { 
	"removeflag", 
	SCT_COMMAND,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE, 
	NULL, NULL,
	ext_imap4flags_command_validate, 
	cmd_flag_generate, 
	NULL 
};

/*
 * Operations
 */

/* Forward declarations */

bool cmd_flag_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_flag_operation_execute
	(const struct sieve_operation *op,	
		const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Setflag operation */

const struct sieve_operation setflag_operation = { 
	"SETFLAG",
	&imap4flags_extension,
	ext_imap4flags_OPERATION_SETFLAG,
	cmd_flag_operation_dump,
	cmd_flag_operation_execute
};

/* Addflag operation */

const struct sieve_operation addflag_operation = { 
	"ADDFLAG",
	&imap4flags_extension,
	ext_imap4flags_OPERATION_ADDFLAG,
	cmd_flag_operation_dump,	
	cmd_flag_operation_execute
};

/* Removeflag operation */

const struct sieve_operation removeflag_operation = { 
	"REMOVEFLAG",
	&imap4flags_extension,
	ext_imap4flags_OPERATION_REMOVEFLAG,
	cmd_flag_operation_dump, 
	cmd_flag_operation_execute 
};

/* 
 * Code generation 
 */

static bool cmd_flag_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
	const struct sieve_command *command = ctx->command;

	/* Emit operation */
	if ( command == &cmd_setflag ) 
		sieve_operation_emit_code(cgenv->sbin, &setflag_operation);
	else if ( command == &cmd_addflag ) 
		sieve_operation_emit_code(cgenv->sbin, &addflag_operation);
	else if ( command == &cmd_removeflag ) 
		sieve_operation_emit_code(cgenv->sbin, &removeflag_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/*
 * Code dump
 */

bool cmd_flag_operation_dump
(const struct sieve_operation *op,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct sieve_operand *operand;

	sieve_code_dumpf(denv, "%s", op->mnemonic);
	sieve_code_descend(denv);
	
	sieve_code_mark(denv);
	operand = sieve_operand_read(denv->sbin, address);
	if ( operand == NULL ) {
		sieve_code_dumpf(denv, "ERROR: INVALID OPERAND");
		return FALSE;
	}

	if ( sieve_operand_is_variable(operand) ) {	
		return 
			sieve_opr_string_dump_data(denv, operand, address, 
				"variable name") &&
			sieve_opr_stringlist_dump(denv, address, 
				"list of flags");
	}
	
	return 
		sieve_opr_stringlist_dump_data(denv, operand, address,
			"list of flags");
}
 
/*
 * Code execution
 */

static int cmd_flag_operation_execute
(const struct sieve_operation *op,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_operand *operand;
	sieve_size_t op_address = *address;
	bool result = TRUE;
	string_t *flag_item;
	struct sieve_coded_stringlist *flag_list;
	struct sieve_variable_storage *storage;
	unsigned int var_index;
	ext_imapflag_flag_operation_t flag_op;
	int ret;
		
	/* 
	 * Read operands 
	 */

	operand = sieve_operand_read(renv->sbin, address);
	if ( operand == NULL ) {
		sieve_runtime_trace_error(renv, "invalid operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
		
	if ( sieve_operand_is_variable(operand) ) {		

		/* Read the variable operand */
		if ( !sieve_variable_operand_read_data
			(renv, operand, address, &storage, &var_index) ) {
			sieve_runtime_trace_error(renv, "invalid variable operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
		
		/* Read flag list */
		if ( (flag_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
			sieve_runtime_trace_error(renv, "invalid flag-list operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}

	} else if ( sieve_operand_is_stringlist(operand) ) {	
		storage = NULL;
		var_index = 0;
		
		/* Read flag list */
		if ( (flag_list=sieve_opr_stringlist_read_data
			(renv, operand, op_address, address)) == NULL ) {
			sieve_runtime_trace_error(renv, "invalid flag-list operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}

	} else {
		sieve_runtime_trace_error(renv, "unexpected operand '%s'", 
			operand->name);
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/*
	 * Perform operation
	 */	
	
	sieve_runtime_trace(renv, "%s command", op->mnemonic);

	/* Determine what to do */

	if ( op == &setflag_operation )
		flag_op = ext_imap4flags_set_flags;
	else if ( op == &addflag_operation )
		flag_op = ext_imap4flags_add_flags;
	else if ( op == &removeflag_operation )
		flag_op = ext_imap4flags_remove_flags;
	else
		i_unreached();

	/* Iterate through all flags and perform requested operation */
	
	while ( (result=sieve_coded_stringlist_next_item(flag_list, &flag_item)) && 
		flag_item != NULL ) {

		if ( (ret=flag_op(renv, storage, var_index, flag_item)) <= 0)
			return ret;
	}

	if ( !result ) {	
		sieve_runtime_trace_error(renv, "invalid flag-list item");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	return SIEVE_EXEC_OK;
}
