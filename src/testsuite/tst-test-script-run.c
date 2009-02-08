/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"
#include "sieve.h"

#include "testsuite-common.h"
#include "testsuite-script.h"
#include "testsuite-result.h"

/*
 * Test_script_run command
 *
 * Syntax:   
 *   test_script_run
 */

static bool tst_test_script_run_registered
(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_test_script_run_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command tst_test_script_run = { 
	"test_script_run", 
	SCT_TEST, 
	0, 0, FALSE, FALSE,
	tst_test_script_run_registered, 
	NULL, NULL,
	tst_test_script_run_generate, 
	NULL 
};

/* 
 * Operation 
 */

static bool tst_test_script_run_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_test_script_run_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation test_script_run_operation = { 
	"test_script_run",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_SCRIPT_RUN,
	tst_test_script_run_operation_dump, 
	tst_test_script_run_operation_execute 
};

/*
 * Tagged arguments
 */

/* Codes for optional arguments */

enum cmd_vacation_optional {
	OPT_END,
	OPT_APPEND_RESULT
};

/* Tags */

static const struct sieve_argument append_result_tag = { 
	"append_result",	
	NULL, NULL, NULL, NULL, NULL
};

static bool tst_test_script_run_registered
(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag
		(validator, cmd_reg, &append_result_tag, OPT_APPEND_RESULT); 	

	return TRUE;
}


/* 
 * Code generation 
 */

static bool tst_test_script_run_generate
(const struct sieve_codegen_env *cgenv, 
	struct sieve_command_context *tst)
{
	sieve_operation_emit_code(cgenv->sbin, &test_script_run_operation);

	return sieve_generate_arguments(cgenv, tst, NULL);
}

/*
 * Code dump
 */

static bool tst_test_script_run_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{	
	int opt_code = 1;
	
	sieve_code_dumpf(denv, "TEST_SCRIPT_RUN");
	sieve_code_descend(denv);	

	/* Dump optional operands */
	if ( sieve_operand_optional_present(denv->sbin, address) ) {
		while ( opt_code != 0 ) {
			sieve_code_mark(denv);
			
			if ( !sieve_operand_optional_read(denv->sbin, address, &opt_code) ) 
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_APPEND_RESULT:
				sieve_code_dumpf(denv, "append_result");	
				break;
			
			default:
				return FALSE;
			}
		}
	}
	
	return TRUE;
}


/*
 * Intepretation
 */

static int tst_test_script_run_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, 
	sieve_size_t *address ATTR_UNUSED)
{
	bool append_result = FALSE;
	int opt_code = 1;
	bool result = TRUE;

	/*
	 * Read operands
	 */

	/* Optional operands */	
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) ) {
				sieve_runtime_trace_error(renv, "invalid optional operand");
				return SIEVE_EXEC_BIN_CORRUPT;
			}

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_APPEND_RESULT:
				append_result = TRUE;
				break;
			default:
				sieve_runtime_trace_error(renv, 
					"unknown optional operand");
				return SIEVE_EXEC_BIN_CORRUPT;
			}
		}
	}

	/*
	 * Perform operation
	 */

	/* Reset result object */
	if ( !append_result ) 
		testsuite_result_reset(renv);

	/* Run script */
	result = testsuite_script_run(renv);

	/* Indicate test status */
	sieve_interpreter_set_test_result(renv->interp, result);

	return SIEVE_EXEC_OK;
}




