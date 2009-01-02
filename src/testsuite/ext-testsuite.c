/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension testsuite
 * -------------------
 *
 * Authors: Stephan Bosch
 * Specification: vendor-specific 
 *   (FIXME: provide specification for test authors)
 * Implementation: very basic
 * Status: under development
 * Purpose: This custom extension is used to add sieve commands and tests that 
 *          act the Sieve engine and on the test suite itself. This practically 
 *          provides the means to completely control and thereby test the Sieve 
 *          compiler and interpreter. This extension transforms the basic Sieve 
 *          language into something much more powerful and suitable to perform 
 *          complex self-test operations. Of course, this extension is only 
 *          available (as vnd.dovecot.testsuite) when the sieve engine is used
 *          from within the testsuite commandline tool. Test scripts have the
 *          extension .svtest by convention to distinguish them from any normal
 *          sieve scripts that may reside in the same directory. 
 *
 * WARNING: Although this code can serve as an example on how to write extensions 
 *          to the Sieve interpreter, it is generally _NOT_ to be used as a source 
 *          for ideas on new Sieve extensions. Many of the commands and tests that 
 *          this extension introduces conflict with the goal and the implied 
 *          restrictions of the Sieve language. These restrictions were put in 
 *          place with good reason. Therefore, do _NOT_ export functionality 
 *          provided by this testsuite extension to your custom extensions that are 
 *          to be put to general use. 
 *
 *          Thank you.
 */

#include <stdio.h>

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "testsuite-common.h"
#include "testsuite-arguments.h"

/* 
 * Operations 
 */

const struct sieve_operation *testsuite_operations[] = { 
	&test_operation, 
	&test_finish_operation,
	&test_fail_operation, 
	&test_set_operation,
	&test_script_compile_operation,
	&test_script_run_operation,
	&test_error_operation,
	&test_result_operation,
	&test_result_execute_operation,
	&test_result_print_operation
};

/* 
 * Operands 
 */

const struct sieve_operand *testsuite_operands[] = { 
	&testsuite_object_operand,
	&testsuite_substitution_operand
};
    
/* 
 * Extension
 */

/* Forward declarations */

static bool ext_testsuite_validator_load(struct sieve_validator *valdtr);
static bool ext_testsuite_generator_load(const struct sieve_codegen_env *cgenv);
static bool ext_testsuite_binary_load(struct sieve_binary *sbin);

/* Extension object */

static int ext_my_id = -1;

const struct sieve_extension testsuite_extension = { 
	"vnd.dovecot.testsuite", 
	&ext_my_id,
	NULL, NULL,
	ext_testsuite_validator_load,
	ext_testsuite_generator_load,
	NULL,
	ext_testsuite_binary_load, 
	NULL, NULL,
	SIEVE_EXT_DEFINE_OPERATIONS(testsuite_operations),
	SIEVE_EXT_DEFINE_OPERANDS(testsuite_operands)
};

/* Extension implementation */

static bool ext_testsuite_validator_load(struct sieve_validator *valdtr)
{
	sieve_validator_register_command(valdtr, &cmd_test);
	sieve_validator_register_command(valdtr, &cmd_test_fail);
	sieve_validator_register_command(valdtr, &cmd_test_set);
	sieve_validator_register_command(valdtr, &cmd_test_result_print);

	sieve_validator_register_command(valdtr, &tst_test_script_compile);
	sieve_validator_register_command(valdtr, &tst_test_script_run);
	sieve_validator_register_command(valdtr, &tst_test_error);
	sieve_validator_register_command(valdtr, &tst_test_result);	
	sieve_validator_register_command(valdtr, &tst_test_result_execute);	

	sieve_validator_argument_override(valdtr, SAT_VAR_STRING,
		&testsuite_string_argument);

	return testsuite_validator_context_initialize(valdtr);
}

static bool ext_testsuite_generator_load(const struct sieve_codegen_env *cgenv)
{
	return testsuite_generator_context_initialize(cgenv->gentr);
}

static bool ext_testsuite_binary_load(struct sieve_binary *sbin ATTR_UNUSED)
{
	return TRUE;
}



