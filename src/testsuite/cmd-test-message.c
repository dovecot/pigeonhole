#include "lib.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "istream.h"
#include "istream-header-filter.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"
#include "sieve-result.h"

#include "testsuite-common.h"

#include <stdio.h>

/* Forward declarations */

static bool cmd_test_message_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool cmd_test_message_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_test_message_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_test_message_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Test command 
 * 
 * Syntax
 *   redirect <address: string>
 */

const struct sieve_command cmd_test_message = { 
	"test_message", 
	SCT_COMMAND,
	1, 0, FALSE, FALSE, 
	NULL, NULL,
	cmd_test_message_validate, 
	cmd_test_message_generate, 
	NULL 
};

/* Test operation */

const struct sieve_operation test_message_operation = { 
	"TEST_MESSAGE",
	&testsuite_extension, 
	0,
	cmd_test_message_operation_dump, 
	cmd_test_message_operation_execute 
};

/* Validation */

static bool cmd_test_message_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	/* Check argument */
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "address", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(validator, cmd, arg, FALSE);
}

/*
 * Generation
 */
 
static bool cmd_test_message_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_operation_ext(generator, &test_message_operation, 
		ext_testsuite_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
	
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_test_message_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "TEST MESSAGE:");
	sieve_code_descend(denv);

	return 
		sieve_opr_string_dump(denv, address);
}

/*
 * Intepretation
 */

static bool cmd_test_message_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *message;

	t_push();

	if ( !sieve_opr_string_read(renv, address, &message) ) {
		t_pop();
		return FALSE;
	}

	printf(">> TEST MESSAGE \"%s\"\n", str_c(message));
	
	t_pop();
	
	return TRUE;
}



