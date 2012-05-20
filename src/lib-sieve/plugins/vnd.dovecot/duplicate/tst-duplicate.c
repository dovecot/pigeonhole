/* Copyright (c) 2002-2012 Sieve duplicate Plugin authors, see the included
 * COPYING file.
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-duplicate-common.h"

/* Duplicate test
 *
 * Syntax:
 *   "duplicate" [<name: string>]
 *
 */

static bool tst_duplicate_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool tst_duplicate_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

const struct sieve_command_def tst_duplicate = {
	"duplicate",
	SCT_TEST,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE,
	NULL,	NULL,
	tst_duplicate_validate,
	NULL,
	tst_duplicate_generate,
	NULL,
};

/* 
 * Duplicate operation 
 */

static bool tst_duplicate_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_duplicate_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def tst_duplicate_operation = { 
	"DUPLICATE", &duplicate_extension, 
	0,
	tst_duplicate_operation_dump,
	tst_duplicate_operation_execute
};

/* 
 * Validation 
 */

static bool tst_duplicate_validate
(struct sieve_validator *valdtr, struct sieve_command *cmd) 
{ 	
	struct sieve_ast_argument *arg = cmd->first_positional;
	
	if ( arg == NULL )
		return TRUE;

	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "name", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/*
 * Code generation
 */

static bool tst_duplicate_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &tst_duplicate_operation);

	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;

	/* Emit a placeholder when the <name> argument is missing */
	if ( cmd->first_positional == NULL )
		sieve_opr_omitted_emit(cgenv->sblock);

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool tst_duplicate_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "DUPLICATE");
	sieve_code_descend(denv);
	
	return sieve_opr_string_dump_ex(denv, address, "name", "");
}

/* 
 * Code execution
 */

static int tst_duplicate_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{	
	string_t *name = NULL;
	bool duplicate = FALSE;
	int ret;

	/*
	 * Read operands
	 */

	/* Read rejection reason */
	if ( (ret=sieve_opr_string_read_ex(renv, address, "name", TRUE, &name, NULL))
		<= 0 )
		return ret;

	/*
	 * Perform operation
	 */

	/* Trace */
	sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "duplicate test");
	sieve_runtime_trace_descend(renv);

	/* Check duplicate */
	if ( (duplicate=ext_duplicate_check(renv, name)) ) {
		sieve_runtime_trace(renv,	SIEVE_TRLVL_TESTS,
			"message is a duplicate");
	}	else {	
		sieve_runtime_trace(renv,	SIEVE_TRLVL_TESTS,
			"message is not a duplicate");
	}
	
	/* Set test result for subsequent conditional jump */
	sieve_interpreter_set_test_result(renv->interp, duplicate);
	return SIEVE_EXEC_OK;
}

