#include "lib.h"

#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"

/* 
 * Removeflag command 
 *
 * Syntax:
 *   removeflag [<variablename: string>] <list-of-flags: string-list>
 */

static bool cmd_removeflag_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);
 
const struct sieve_command cmd_removeflag = { 
	"removeflag", 
	SCT_COMMAND,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE, 
	NULL, NULL,
	ext_imapflags_command_validate, 
	cmd_removeflag_generate, 
	NULL 
};

/* 
 * Removeflag operation 
 */

static int cmd_removeflag_operation_execute
	(const struct sieve_operation *op,	
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation removeflag_operation = { 
	"REMOVEFLAG",
	&imapflags_extension,
	EXT_IMAPFLAGS_OPERATION_REMOVEFLAG,
	ext_imapflags_command_operation_dump, 
	cmd_removeflag_operation_execute 
};

/* 
 * Code generation 
 */

static bool cmd_removeflag_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
	sieve_operation_emit_code(cgenv->sbin, &removeflag_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
		return FALSE;	

	return TRUE;
}
 
/*
 * Execution
 */

static int cmd_removeflag_operation_execute
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
		(renv, address, &flag_list, &storage, &var_index)) <= 0 )
		return ret;
	
	sieve_runtime_trace(renv, "REMOVEFLAG command");

	/* Iterate through all flags to remove */
	while ( (result=sieve_coded_stringlist_next_item(flag_list, &flag_item)) && 
		flag_item != NULL ) {

		if ( (ret=ext_imapflags_remove_flags(renv, storage, var_index, flag_item)) <= 0)
			return ret;
	}

	if ( !result ) {	
		sieve_runtime_trace_error(renv, "invalid flag-list item");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	return SIEVE_EXEC_OK;
}
