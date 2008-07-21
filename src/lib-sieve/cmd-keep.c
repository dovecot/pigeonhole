/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-dump.h"
#include "sieve-actions.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

/* 
 * Keep command 
 *
 * Syntax:
 *   keep
 */	

static bool cmd_keep_generate
	(const struct sieve_codegen_env *cgenv, 
		struct sieve_command_context *ctx);

const struct sieve_command cmd_keep = { 
	"keep", 
	SCT_COMMAND, 
	0, 0, FALSE, FALSE,
	NULL, NULL, NULL, 
	cmd_keep_generate, 
	NULL
};

/* 
 * Keep operation 
 */

static bool cmd_keep_operation_dump
	(const struct sieve_operation *op,
    	const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool cmd_keep_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation cmd_keep_operation = { 
	"KEEP",
	NULL,
	SIEVE_OPERATION_KEEP,
	cmd_keep_operation_dump, 
	cmd_keep_operation_execute 
};

/*
 * Code generation
 */

static bool cmd_keep_generate
(const struct sieve_codegen_env *cgenv, 
	struct sieve_command_context *ctx ATTR_UNUSED) 
{
	/* Emit opcode */
	sieve_operation_emit_code(cgenv->sbin, &cmd_keep_operation, -1);

	/* Emit line number */
    sieve_code_source_line_emit(cgenv->sbin, sieve_command_source_line(ctx));

	return TRUE;
}

/* 
 * Code dump
 */

static bool cmd_keep_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
    const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
    sieve_code_dumpf(denv, "KEEP");
    sieve_code_descend(denv);

    /* Source line */
    if ( !sieve_code_source_line_dump(denv, address) )
        return FALSE;

    return sieve_code_dumper_print_optional_operands(denv, address);
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
	unsigned int source_line;
	int ret = 0;	

	sieve_runtime_trace(renv, "KEEP action");

	/* Source line */
    if ( !sieve_code_source_line_read(renv, address, &source_line) )
        return FALSE;
	
	/* Optional operands (side effects only) */
	if ( !sieve_interpreter_handle_optional_operands(renv, address, &slist) )
		return FALSE;
	
	/* Add store action (sieve-actions.h) to result */
	if ( renv->scriptenv != NULL && renv->scriptenv->inbox != NULL )
		ret = sieve_act_store_add_to_result
			(renv, slist, renv->scriptenv->inbox, source_line);
	else
		ret = sieve_act_store_add_to_result
			(renv, slist, "INBOX", source_line);
	
	return ret >= 0;
}


