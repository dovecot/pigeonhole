/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension include
 * -----------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-daboo-sieve-include-05
 * Implementation: full, but needs some more work 
 * Status: experimental, largely untested
 * 
 */
 
/* FIXME: Current include implementation does not allow for parts of the script
 * to be located in external binaries; all included scripts are recompiled and
 * the resulting byte code is imported into the main binary in separate blocks.
 */
 
#include "lib.h"

#include "sieve-common.h"

#include "sieve-extensions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "ext-include-common.h"
#include "ext-include-binary.h"

/* 
 * Operations 
 */

static const struct sieve_operation *ext_include_operations[] = { 
	&include_operation, 
	&return_operation,
	&import_operation,
	&export_operation
};

/* 
 * Extension
 */
 
/* Forward declaration */

static bool ext_include_validator_load(struct sieve_validator *validator);
static bool ext_include_generator_load(const struct sieve_codegen_env *cgenv);
static bool ext_include_interpreter_load
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Extension objects */

static int ext_my_id = -1;

const struct sieve_extension include_extension = { 
	"include", 
	&ext_my_id,
	NULL, NULL,
	ext_include_validator_load, 
	ext_include_generator_load,
	ext_include_interpreter_load,
	NULL,
	ext_include_binary_dump,
	ext_include_code_dump,
	SIEVE_EXT_DEFINE_OPERATIONS(ext_include_operations),
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

/* Extension hooks */

static bool ext_include_validator_load(struct sieve_validator *validator)
{
	/* Register new commands */
	sieve_validator_register_command(validator, &cmd_include);
	sieve_validator_register_command(validator, &cmd_return);
	sieve_validator_register_command(validator, &cmd_import);
	sieve_validator_register_command(validator, &cmd_export);

	return TRUE;
}	

static bool ext_include_generator_load(const struct sieve_codegen_env *cgenv)
{
	ext_include_register_generator_context(cgenv);

	return TRUE;
}

static bool ext_include_interpreter_load
(const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{
	ext_include_interpreter_context_init(renv->interp);
	
	return TRUE;
}

