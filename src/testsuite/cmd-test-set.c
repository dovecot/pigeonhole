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
#include "testsuite-objects.h"

#include <stdio.h>

/* 
 * Test_set command 
 * 
 * Syntax
 *   redirect <address: string>
 */

static bool cmd_test_set_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_test_set_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command cmd_test_set = { 
	"test_set", 
	SCT_COMMAND,
	2, 0, FALSE, FALSE, 
	NULL, NULL,
	cmd_test_set_validate, 
	cmd_test_set_generate, 
	NULL 
};

/* 
 * Test_set operation 
 */

static bool cmd_test_set_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_test_set_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation test_set_operation = { 
	"TEST_SET",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_SET,
	cmd_test_set_operation_dump, 
	cmd_test_set_operation_execute 
};

/* 
 * Validation 
 */
 
static bool cmd_test_set_validate
(struct sieve_validator *valdtr, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	/* Check arguments */
	
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "object", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	if ( !testsuite_object_argument_activate(valdtr, arg, cmd) )
		return FALSE;
	
	arg = sieve_ast_argument_next(arg);
	
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "value", 2, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/*
 * Generation
 */
 
static bool cmd_test_set_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	sieve_operation_emit_code(cgenv->sbin, &test_set_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, ctx, NULL);
}

/* 
 * Code dump
 */
 
static bool cmd_test_set_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "TEST SET:");
	sieve_code_descend(denv);

	return 
		testsuite_object_dump(denv, address) &&
		sieve_opr_string_dump(denv, address);
}

/*
 * Intepretation
 */

static int cmd_test_set_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct testsuite_object *object;
	string_t *value;
	int member_id;

	if ( (object=testsuite_object_read_member(renv->sbin, address, &member_id)) 
		== NULL ) {
		sieve_runtime_trace_error(renv, "invalid testsuite object member");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	if ( !sieve_opr_string_read(renv, address, &value) ) {
		sieve_runtime_trace_error(renv, "invalid string operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	sieve_runtime_trace(renv, "TEST SET command (%s = \"%s\")", 
		testsuite_object_member_name(object, member_id), str_c(value));
	
	if ( object->set_member == NULL ) {
		sieve_runtime_trace_error(renv, "unimplemented testsuite object");
		return SIEVE_EXEC_FAILURE;
	}
		
	object->set_member(member_id, value);	
	return SIEVE_EXEC_OK;
}



