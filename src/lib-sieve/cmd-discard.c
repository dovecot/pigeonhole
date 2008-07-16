#include "lib.h"

#include "sieve-script.h"
#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-dump.h"
#include "sieve-actions.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

/* Forward declarations */

static bool cmd_discard_generate
	(const struct sieve_codegen_env *cgenv, 
		struct sieve_command_context *ctx ATTR_UNUSED); 

static bool cmd_discard_operation_dump
	(const struct sieve_operation *op,
    	const struct sieve_dumptime_env *denv, sieve_size_t *address);
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
	cmd_discard_operation_dump, 
	cmd_discard_operation_execute 
};

/* discard action */

static void act_discard_print
	(const struct sieve_action *action, const struct sieve_result_print_env *rpenv,
		void *context, bool *keep);	
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
	(const struct sieve_codegen_env *cgenv, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_operation_emit_code(cgenv->sbin, &cmd_discard_operation, -1);

	/* Emit line number */
    sieve_code_source_line_emit(cgenv->sbin, sieve_command_source_line(ctx));

	return TRUE;
}

/* 
 * Code dump
 */

static bool cmd_discard_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
    const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
    sieve_code_dumpf(denv, "DISCARD");
    sieve_code_descend(denv);

    /* Source line */
    if ( !sieve_code_source_line_dump(denv, address) )
        return FALSE;

    return sieve_code_dumper_print_optional_operands(denv, address);
}

/*
 * Interpretation
 */

static bool cmd_discard_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	unsigned int source_line;

	sieve_runtime_trace(renv, "DISCARD action");
	
	/* Source line */
    if ( !sieve_code_source_line_read(renv, address, &source_line) )
        return FALSE;

	return ( sieve_result_add_action(renv, &act_discard, NULL, 
		sieve_script_name(renv->script), source_line, NULL) >= 0 );
}

/*
 * Action
 */
 
static void act_discard_print
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_result_print_env *rpenv, void *context ATTR_UNUSED, 
	bool *keep)	
{
	sieve_result_action_printf(rpenv, "discard");
	
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

