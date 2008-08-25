/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "testsuite-common.h"

/*
 * Test command
 *
 * Syntax:   
 *   test <test-name: string> <block>
 */

static bool cmd_test_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_test_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command cmd_test = { 
	"test", 
	SCT_COMMAND, 
	1, 0, TRUE, TRUE,
	NULL, NULL,
	cmd_test_validate, 
	cmd_test_generate, 
	NULL 
};

/* 
 * Test operations 
 */

/* Test operation */

static bool cmd_test_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_test_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation test_operation = { 
	"TEST",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST,
	cmd_test_operation_dump, 
	cmd_test_operation_execute 
};

/* Test_finish operation */

static int cmd_test_finish_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation test_finish_operation = { 
	"TEST-FINISH",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_FINISH,
	NULL, 
	cmd_test_finish_operation_execute 
};

/* 
 * Validation 
 */

static bool cmd_test_validate
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;
		
 	/* Check valid command placement */
	if ( !sieve_command_is_toplevel(cmd) )
	{
		sieve_command_validate_error(valdtr, cmd,
			"tests cannot be nested: test command must be issued at top-level");
		return FALSE;
	}
	
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "test-name", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/* 
 * Code generation 
 */

static inline struct testsuite_generator_context *
	_get_generator_context(struct sieve_generator *gentr)
{
	return (struct testsuite_generator_context *) 
		sieve_generator_extension_get_context(gentr, &testsuite_extension);
}

static bool cmd_test_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
	struct testsuite_generator_context *genctx = 
		_get_generator_context(cgenv->gentr);
	
	sieve_operation_emit_code(cgenv->sbin, &test_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
		return FALSE;
		
	/* Prepare jumplist */
	sieve_jumplist_reset(genctx->exit_jumps);
		
	/* Test body */
	sieve_generate_block(cgenv, ctx->ast_node);
	
	sieve_operation_emit_code(cgenv->sbin, &test_finish_operation);
	
	/* Resolve exit jumps to this point */
	sieve_jumplist_resolve(genctx->exit_jumps); 
			
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_test_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "TEST:");
	sieve_code_descend(denv);

	return 
		sieve_opr_string_dump(denv, address);
}

/*
 * Intepretation
 */

static int cmd_test_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *test_name;

	if ( !sieve_opr_string_read(renv, address, &test_name) ) {
		sieve_runtime_trace_error(renv, "invalid test name operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	sieve_runtime_trace(renv, "TEST \"%s\"", str_c(test_name));

	testsuite_test_start(test_name);
	return SIEVE_EXEC_OK;
}

static int cmd_test_finish_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{
	sieve_runtime_trace(renv, "TEST FINISHED");
	
	testsuite_test_succeed(NULL);
	return SIEVE_EXEC_OK;
}





