#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "testsuite-common.h"

/* Predeclarations */

static bool cmd_test_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool cmd_test_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_test_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_test_generate
	(struct sieve_generator *generator, struct sieve_command_context *ctx);

/* Test command
 *
 * Syntax:   
 *   test <test-name: string> <block>
 */
const struct sieve_command cmd_test = { 
	"test", 
	SCT_COMMAND, 
	1, 0, TRUE, TRUE,
	NULL, NULL,
	cmd_test_validate, 
	cmd_test_generate, 
	NULL 
};

/* Test operation */

const struct sieve_operation test_operation = { 
	"TEST",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST,
	cmd_test_operation_dump, 
	cmd_test_operation_execute 
};

/* Validation */

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

/* Code generation */

static bool cmd_test_generate
	(struct sieve_generator *generator, struct sieve_command_context *ctx)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	struct sieve_jumplist jmplist;
	
	sieve_generator_emit_operation_ext(generator, &test_operation, 
		ext_testsuite_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
		
	/* Prepare jumplist */
	sieve_jumplist_init_temp(&jmplist, sbin);
		
	/* Test body */
	sieve_generate_block(generator, ctx->ast_node);
	
	/* Resolve exit jumps to this point */
	sieve_jumplist_resolve(&jmplist); 
			
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

static bool cmd_test_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *test_name;

	t_push();

	if ( !sieve_opr_string_read(renv, address, &test_name) ) {
		t_pop();
		return FALSE;
	}

	printf("TEST: %s\n", str_c(test_name));
	
	t_pop();
	
	return TRUE;
}




