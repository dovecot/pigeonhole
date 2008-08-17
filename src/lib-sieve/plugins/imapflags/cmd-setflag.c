/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"

/* 
 * Setflag command 
 *
 * Syntax: 
 *   setflag [<variablename: string>] <list-of-flags: string-list>
 */

static bool cmd_setflag_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);
 
const struct sieve_command cmd_setflag = { 
	"setflag", 
	SCT_COMMAND,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE, 
	NULL, NULL,
	ext_imapflags_command_validate, 
	cmd_setflag_generate, 
	NULL 
};

/* 
 * Setflag operation 
 */

static int cmd_setflag_operation_execute
	(const struct sieve_operation *op,
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation setflag_operation = { 
	"SETFLAG",
	&imapflags_extension,
	EXT_IMAPFLAGS_OPERATION_SETFLAG,
	ext_imapflags_command_operation_dump,
	cmd_setflag_operation_execute
};

/* 
 * Code generation 
 */

static bool cmd_setflag_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
	sieve_operation_emit_code(cgenv->sbin, &setflag_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/*
 * Execution
 */

static int cmd_setflag_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool result = TRUE;
	string_t *flag_item;
	struct sieve_coded_stringlist *flag_list;
	struct sieve_variable_storage *storage;
	unsigned int var_index;
	int ret;
		
	if ( (ret=ext_imapflags_command_operands_read
		(renv, address, &flag_list, &storage, &var_index)) <=0 ) 
		return ret;

	sieve_runtime_trace(renv, "SETFLAG command");
			
	/* Iterate through all flags to set */
	while ( (result=sieve_coded_stringlist_next_item(flag_list, &flag_item)) && 
		flag_item != NULL ) {

		if ( (ret=ext_imapflags_set_flags(renv, storage, var_index, flag_item)) <= 0)
			return ret;
	}

	if ( !result ) {
		sieve_runtime_trace_error(renv, "invalid flag-list item operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	return SIEVE_EXEC_OK;
}

