/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
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
#include "testsuite-binary.h"
#include "testsuite-script.h"

/*
 * Test_binary command
 *
 * Syntax:   
 *   test_binary ( :load / :save ) <mailbox: string>
 */

static bool cmd_test_binary_registered
	(struct sieve_validator *valdtr, 
		struct sieve_command_registration *cmd_reg);
static bool cmd_test_binary_validate
	(struct sieve_validator *valdtr, struct sieve_command_context *cmd);
static bool cmd_test_binary_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command cmd_test_binary = { 
	"test_binary", 
	SCT_COMMAND, 
	1, 0, FALSE, FALSE,
	cmd_test_binary_registered, 
	NULL,
	cmd_test_binary_validate, 
	cmd_test_binary_generate, 
	NULL 
};

/* 
 * Operations
 */ 

static bool cmd_test_binary_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_test_binary_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);
 
/* test_binary_create operation */

const struct sieve_operation test_binary_load_operation = { 
	"TEST_BINARY_LOAD",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_BINARY_LOAD,
	cmd_test_binary_operation_dump, 
	cmd_test_binary_operation_execute 
};

/* test_binary_delete operation */

const struct sieve_operation test_binary_save_operation = { 
	"TEST_BINARY_SAVE",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_BINARY_SAVE,
	cmd_test_binary_operation_dump, 
	cmd_test_binary_operation_execute 
};

/*
 * Compiler context data
 */
 
enum test_binary_operation {
	BINARY_OP_LOAD, 
	BINARY_OP_SAVE,
	BINARY_OP_LAST
};

const struct sieve_operation *test_binary_operations[] = {
	&test_binary_load_operation,
	&test_binary_save_operation
};

struct cmd_test_binary_context_data {
	enum test_binary_operation binary_op;
	const char *folder;
};

/* 
 * Command tags 
 */
 
static bool cmd_test_binary_validate_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);

static const struct sieve_argument test_binary_load_tag = { 
	"load", 
	NULL, NULL,
	cmd_test_binary_validate_tag,
	NULL, NULL 
};

static const struct sieve_argument test_binary_save_tag = { 
	"save", 
	NULL, NULL, 
	cmd_test_binary_validate_tag,
	NULL, NULL 
};

static bool cmd_test_binary_registered
(struct sieve_validator *valdtr, struct sieve_command_registration *cmd_reg) 
{
	/* Register our tags */
	sieve_validator_register_tag(valdtr, cmd_reg, &test_binary_load_tag, 0); 	
	sieve_validator_register_tag(valdtr, cmd_reg, &test_binary_save_tag, 0); 	

	return TRUE;
}

static bool cmd_test_binary_validate_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct cmd_test_binary_context_data *ctx_data = 
		(struct cmd_test_binary_context_data *) cmd->data;	
	
	if ( ctx_data != NULL ) {
		sieve_argument_validate_error
			(valdtr, *arg, "exactly one of the ':load' or ':save' tags must be "
				"specified for the test_binary command, but more were found");
		return NULL;		
	}
	
	ctx_data = p_new
		(sieve_command_pool(cmd), struct cmd_test_binary_context_data, 1);
	cmd->data = ctx_data;
	
	if ( (*arg)->argument == &test_binary_load_tag ) 
		ctx_data->binary_op = BINARY_OP_LOAD;
	else
		ctx_data->binary_op = BINARY_OP_SAVE;

	/* Delete this tag */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	return TRUE;
}

/* 
 * Validation 
 */

static bool cmd_test_binary_validate
(struct sieve_validator *valdtr, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	
	if ( cmd->data == NULL ) {
		sieve_command_validate_error(valdtr, cmd, 
			"the test_binary command requires either the :load or the :save tag "
			"to be specified");
		return FALSE;		
	}
		
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "binary-name", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/* 
 * Code generation 
 */

static bool cmd_test_binary_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *cmd)
{
	struct cmd_test_binary_context_data *ctx_data =
		(struct cmd_test_binary_context_data *) cmd->data; 

	i_assert( ctx_data->binary_op < BINARY_OP_LAST );
	
	/* Emit operation */
	sieve_operation_emit_code(cgenv->sbin, 
		test_binary_operations[ctx_data->binary_op]);
	  	
 	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_test_binary_operation_dump
(const struct sieve_operation *op,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "%s:", op->mnemonic);
	
	sieve_code_descend(denv);
	
	return sieve_opr_string_dump(denv, address, "binary-name");
}


/*
 * Intepretation
 */
 
static int cmd_test_binary_operation_execute
(const struct sieve_operation *op,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *binary_name = NULL;

	/* 
	 * Read operands 
	 */

	/* Binary Name */

	if ( !sieve_opr_string_read(renv, address, &binary_name) ) {
		sieve_runtime_trace_error(renv, "invalid mailbox operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/*
	 * Perform operation
	 */
		
	sieve_runtime_trace(renv, "%s %s:", op->mnemonic, str_c(binary_name));

	if ( op == &test_binary_load_operation ) {
		struct sieve_binary *sbin = testsuite_binary_load(str_c(binary_name));

		if ( sbin != NULL ) {
			testsuite_script_set_binary(sbin);

			sieve_binary_unref(&sbin);
		} else {
			sieve_sys_error("failed to load binary %s", str_c(binary_name));
			return SIEVE_EXEC_FAILURE;
		}

	} else if ( op == &test_binary_save_operation ) {
		struct sieve_binary *sbin = testsuite_script_get_binary();

		if ( sbin != NULL ) 
			testsuite_binary_save(sbin, str_c(binary_name));
		else {
			sieve_sys_error("no compiled binary to save as %s", str_c(binary_name));
			return SIEVE_EXEC_FAILURE;
		}
	}

	return SIEVE_EXEC_OK;
}
