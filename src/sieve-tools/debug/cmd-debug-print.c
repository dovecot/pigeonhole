/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */
 
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-code.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-debug-common.h"

/* 
 * Debug_print command
 *
 * Syntax
 *   debug_print <message: string>
 */

static bool cmd_debug_print_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool cmd_debug_print_generate
	(const struct sieve_codegen_env *cgenv,	struct sieve_command_context *ctx);

const struct sieve_command debug_print_command = { 
	"debug_print", 
	SCT_COMMAND, 
	1, 0, FALSE, FALSE,
	NULL, NULL,
	cmd_debug_print_validate, 
	cmd_debug_print_generate, 
	NULL 
};

/* 
 * Body operation 
 */

static bool cmd_debug_print_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_debug_print_operation_execute
	(const struct sieve_operation *op,
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation debug_print_operation = { 
	"debug_print",
	&debug_extension,
	0,
	cmd_debug_print_operation_dump, 
	cmd_debug_print_operation_execute 
};

/* 
 * Validation 
 */
 
static bool cmd_debug_print_validate
(struct sieve_validator *validator, struct sieve_command_context *tst) 
{ 		
	struct sieve_ast_argument *arg = tst->first_positional;
					
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "message", 1, SAAT_STRING) ) {
		return FALSE;
	}

	return sieve_validator_argument_activate(validator, tst, arg, FALSE);
}

/*
 * Code generation
 */
 
static bool cmd_debug_print_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	(void)sieve_operation_emit_code(cgenv->sbin, &debug_print_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, ctx, NULL);
}

/* 
 * Code dump 
 */
 
static bool cmd_debug_print_operation_dump
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "DEBUG_PRINT");
	sieve_code_descend(denv);

	return sieve_opr_string_dump(denv, address, "key list");
}

/*
 * Interpretation
 */

static int cmd_debug_print_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *message;
	int ret = SIEVE_EXEC_OK;

	/*
	 * Read operands
	 */
	
	/* Read message */

	if ( sieve_opr_string_read(renv, address, &message) < 0 ) {
		sieve_runtime_trace_error(renv, "invalid message operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, "DEBUG_PRINT");
	
	/* FIXME: give this proper source location */
	sieve_runtime_log(renv, "DEBUG", "%s", str_c(message));

	return ret;
}
