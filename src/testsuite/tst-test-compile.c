#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"
#include "sieve.h"

#include "testsuite-common.h"

/*
 * Test_compile command
 *
 * Syntax:   
 *   test_compile <scriptpath: string>
 */

static bool tst_test_compile_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool tst_test_compile_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command tst_test_compile = { 
	"test_compile", 
	SCT_TEST, 
	1, 0, FALSE, FALSE,
	NULL, NULL,
	tst_test_compile_validate, 
	tst_test_compile_generate, 
	NULL 
};

/* Test_compile operation */

static bool tst_test_compile_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_test_compile_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation test_compile_operation = { 
	"TEST_COMPILE",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_COMPILE,
	tst_test_compile_operation_dump, 
	tst_test_compile_operation_execute 
};

/* Validation */

static bool tst_test_compile_validate
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_command_context *tst) 
{
	struct sieve_ast_argument *arg = tst->first_positional;
	
	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "script", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(valdtr, tst, arg, FALSE);
}

/* Code generation */

static inline struct testsuite_generator_context *
	_get_generator_context(struct sieve_generator *gentr)
{
	return (struct testsuite_generator_context *) 
		sieve_generator_extension_get_context(gentr, &testsuite_extension);
}

static bool tst_test_compile_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *tst)
{
	sieve_operation_emit_code(cgenv->sbin, &test_compile_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

/* 
 * Code dump
 */
 
static bool tst_test_compile_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "TEST_COMPILE:");
	sieve_code_descend(denv);

	if ( !sieve_opr_string_dump(denv, address) ) 
		return FALSE;

	return TRUE;
}

/*
 * Intepretation
 */

static int tst_test_compile_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *script_name;
	const char *script_path;
	bool result = TRUE;

	/*
	 * Read operands
	 */

	if ( !sieve_opr_string_read(renv, address, &script_name) ) {
		sieve_runtime_trace_error(renv, "invalid script name operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, "TEST COMPILE: %s", str_c(script_name));

	script_path = sieve_script_dirpath(renv->script);
	if ( script_path == NULL ) 
		return SIEVE_EXEC_FAILURE;

	script_path = t_strconcat(script_path, "/", str_c(script_name), NULL);

	/* Attempt script compile */

	result = testsuite_script_compile(script_path);

	/* Set result */
	sieve_interpreter_set_test_result(renv->interp, result);

	return SIEVE_EXEC_OK;
}




