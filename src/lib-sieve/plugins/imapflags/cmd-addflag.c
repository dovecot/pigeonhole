#include "lib.h"

#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"

/* Forward declarations */

static bool cmd_addflag_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);
	
static bool cmd_addflag_operation_execute
	(const struct sieve_operation *op,	
		const struct sieve_runtime_env *renv, sieve_size_t *address);


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
	ext_imapflags_command_validate, 
	cmd_addflag_generate, 
	NULL 
};

/* Addflag operation */

const struct sieve_operation addflag_operation = { 
	"ADDFLAG",
	&imapflags_extension,
	EXT_IMAPFLAGS_OPERATION_ADDFLAG,
	ext_imapflags_command_operation_dump,
	cmd_addflag_operation_execute
};

/* Code generation */

static bool cmd_addflag_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
	sieve_operation_emit_code	
		(cgenv->sbin, &addflag_operation, ext_imapflags_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/*
 * Execution
 */

static bool cmd_addflag_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool result = TRUE;
	string_t *flag_item;
	struct sieve_coded_stringlist *flag_list;
	struct sieve_variable_storage *storage;
	unsigned int var_index;
	
	sieve_runtime_trace(renv, "ADDFLAG command");
	
	t_push();
	
	if ( !ext_imapflags_command_operands_read
		(renv, address, &flag_list, &storage, &var_index) ) {
		t_pop();
		return FALSE;
	}
	
	/* Iterate through all added flags */	
	while ( (result=sieve_coded_stringlist_next_item(flag_list, &flag_item)) && 
		flag_item != NULL ) {
		ext_imapflags_add_flags(renv, storage, var_index, flag_item);
	}

	t_pop();
	
	return result;
}
