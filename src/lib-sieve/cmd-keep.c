#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

/* Forward declarations */

static bool cmd_keep_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_keep_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED);
		
/* Keep command 
 * 
 * Syntax
 *   keep
 */	
const struct sieve_command cmd_keep = { 
	"keep", 
	SCT_COMMAND, 
	0, 0, FALSE, FALSE,
	NULL, NULL, NULL, 
	cmd_keep_generate, 
	NULL
};

/* Keep operation */

const struct sieve_operation cmd_keep_operation = { 
	"KEEP",
	NULL,
	SIEVE_OPERATION_KEEP,
	NULL, 
	cmd_keep_operation_execute 
};

/*
 * Generation
 */

static bool cmd_keep_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_operation_emit_code(
        sieve_generator_get_binary(generator), &cmd_keep_operation, -1);
	return TRUE;
}

/*
 * Interpretation
 */

static bool cmd_keep_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	struct sieve_side_effects_list *slist = NULL;
	int ret = 0;	

	sieve_runtime_trace(renv, "KEEP action");
	
	if ( !sieve_interpreter_handle_optional_operands(renv, address, &slist) )
		return FALSE;
	
	if ( renv->scriptenv != NULL && renv->scriptenv->inbox != NULL )
		ret = sieve_act_store_add_to_result(renv, slist, renv->scriptenv->inbox);
	else
		ret = sieve_act_store_add_to_result(renv, slist, "INBOX");
	
	return ret >= 0;
}


