#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "testsuite-common.h"

/* Predeclarations */

static bool cmd_test_fail_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool cmd_test_fail_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_test_fail_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_test_fail_generate
	(struct sieve_generator *generator, struct sieve_command_context *ctx);

/* Test_fail command
 *
 * Syntax:   
 *   test <reason: string>
 */
const struct sieve_command cmd_test_fail = { 
	"test_fail", 
	SCT_COMMAND, 
	1, 0, FALSE, FALSE,
	NULL, NULL,
	cmd_test_fail_validate, 
	cmd_test_fail_generate, 
	NULL 
};

/* Test operation */

const struct sieve_operation test_fail_operation = { 
	"TEST_FAIL",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_FAIL,
	cmd_test_fail_operation_dump, 
	cmd_test_fail_operation_execute 
};

/* Validation */

static bool cmd_test_fail_validate
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "reason", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/* Code generation */

static inline struct testsuite_generator_context *
	_get_generator_context(struct sieve_generator *gentr)
{
	return (struct testsuite_generator_context *) 
		sieve_generator_extension_get_context(gentr, ext_testsuite_my_id);
}

static bool cmd_test_fail_generate
	(struct sieve_generator *gentr, struct sieve_command_context *ctx)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(gentr);
	struct testsuite_generator_context *genctx = 
		_get_generator_context(gentr);
	
	sieve_generator_emit_operation_ext(gentr, &test_fail_operation, 
		ext_testsuite_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(gentr, ctx, NULL) )
		return FALSE;
		
	sieve_jumplist_add(genctx->exit_jumps, sieve_binary_emit_offset(sbin, 0));			
	
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_test_fail_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	unsigned int pc;
  int offset;
    
	sieve_code_dumpf(denv, "TEST_FAIL:");
	sieve_code_descend(denv);

	if ( !sieve_opr_string_dump(denv, address) ) 
		return FALSE;

	sieve_code_mark(denv);
	pc = *address;
	if ( sieve_binary_read_offset(denv->sbin, address, &offset) )
		sieve_code_dumpf(denv, "OFFSET: %d [%08x]", offset, pc + offset);
	else
		return FALSE;

	return TRUE;
}

/*
 * Intepretation
 */

static bool cmd_test_fail_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *reason;

	t_push();

	if ( !sieve_opr_string_read(renv, address, &reason) ) {
		t_pop();
		return FALSE;
	}

	printf("TEST FAILED: %s\n", str_c(reason));
	
	t_pop();
	
	return sieve_interpreter_program_jump(renv->interp, TRUE);
}




