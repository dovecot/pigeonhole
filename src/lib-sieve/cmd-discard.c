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

static bool cmd_discard_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED); 

static bool cmd_discard_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);
		
/* Discard command 
 * 
 * Syntax
 *   discard
 */	
const struct sieve_command cmd_discard = { 
	"discard", 
	SCT_COMMAND, 
	0, 0, FALSE, FALSE,
	NULL, NULL, NULL, 
	cmd_discard_generate, 
	NULL 
};

/* Discard operation */

const struct sieve_operation cmd_discard_operation = { 
	"DISCARD",
	NULL,
	SIEVE_OPERATION_DISCARD,
	NULL, 
	cmd_discard_operation_execute 
};

/* discard action */

static void act_discard_print
	(const struct sieve_action *action, void *context, bool *keep);	
static bool act_discard_commit
(const struct sieve_action *action, 
	const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep);
		
const struct sieve_action act_discard = {
	"discard",
	0,
	NULL, NULL,
	act_discard_print,
	NULL, NULL,
	act_discard_commit,
	NULL
};


/*
 * Generation
 */
 
static bool cmd_discard_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_operation_emit_code(
        sieve_generator_get_binary(generator), &cmd_discard_operation, -1);
	return TRUE;
}

/*
 * Interpretation
 */

static bool cmd_discard_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	printf(">> DISCARD\n");
	
	return ( sieve_result_add_action(renv, &act_discard, NULL, NULL) >= 0 );
}

/*
 * Action
 */
 
static void act_discard_print
(const struct sieve_action *action ATTR_UNUSED, void *context ATTR_UNUSED, 
	bool *keep)	
{
	printf("* discard\n");
	
	*keep = FALSE;
}

static bool act_discard_commit
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv ATTR_UNUSED, 
	void *tr_context ATTR_UNUSED, bool *keep)
{
	*keep = FALSE;
  return TRUE;
}

