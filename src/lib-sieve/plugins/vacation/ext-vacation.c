/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension vacation
 * ------------------
 *
 * Authors: Stephan Bosch <stephan@rename-it.nl>
 * Specification: RFC 5230
 * Implementation: full
 * Status: testing
 *
 */

#include "lib.h"

#include "sieve-common.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-vacation-common.h"

/*
 * Extension
 */

static bool
ext_vacation_validator_load(const struct sieve_extension *ext,
			   struct sieve_validator *valdtr);
static bool
ext_vacation_interpreter_load(const struct sieve_extension *ext,
			      const struct sieve_runtime_env *renv,
			      sieve_size_t *address);

static bool
ext_vacation_validator_validate(const struct sieve_extension *ext,
				struct sieve_validator *valdtr, void *context,
				struct sieve_ast_argument *require_arg,
				bool required);
static int
ext_vacation_interpreter_run(const struct sieve_extension *this_ext,
			     const struct sieve_runtime_env *renv,
			     void *context, bool deferred);

const struct sieve_extension_def vacation_extension = {
	.name = "vacation",
	.load = ext_vacation_load,
	.unload = ext_vacation_unload,
	.validator_load = ext_vacation_validator_load,
	.interpreter_load = ext_vacation_interpreter_load,
	SIEVE_EXT_DEFINE_OPERATION(vacation_operation)
};
const struct sieve_validator_extension
vacation_validator_extension = {
	.ext = &vacation_extension,
	.validate = ext_vacation_validator_validate
};
const struct sieve_interpreter_extension
vacation_interpreter_extension = {
	.ext_def = &vacation_extension,
	.run = ext_vacation_interpreter_run
};

static bool
ext_vacation_validator_load(const struct sieve_extension *ext,
			    struct sieve_validator *valdtr)
{
	/* Register new command */
	sieve_validator_register_command(valdtr, ext, &vacation_command);

	sieve_validator_extension_register(valdtr, ext,
					   &vacation_validator_extension, NULL);
	return TRUE;
}

static bool
ext_vacation_interpreter_load(const struct sieve_extension *ext,
			      const struct sieve_runtime_env *renv,
			      sieve_size_t *address ATTR_UNUSED)
{
	sieve_interpreter_extension_register(
		renv->interp, ext, &vacation_interpreter_extension, NULL);
	return TRUE;
}

static bool
ext_vacation_validator_validate(const struct sieve_extension *ext,
				struct sieve_validator *valdtr,
				void *context ATTR_UNUSED,
				struct sieve_ast_argument *require_arg,
				bool required)
{
	if (required) {
		enum sieve_compile_flags flags =
			sieve_validator_compile_flags(valdtr);

		if ((flags & SIEVE_COMPILE_FLAG_NO_ENVELOPE) != 0) {
			sieve_argument_validate_error(
				valdtr, require_arg,
				"the %s extension cannot be used in this context "
				"(needs access to message envelope)",
				sieve_extension_name(ext));
			return FALSE;
		}
	}
	return TRUE;
}

static int
ext_vacation_interpreter_run(const struct sieve_extension *ext,
			     const struct sieve_runtime_env *renv,
			     void *context ATTR_UNUSED, bool deferred)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	if ((eenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) != 0) {
		if (!deferred) {
			sieve_runtime_error(
				renv, NULL,
				"the %s extension cannot be used in this context "
				"(needs access to message envelope)",
				sieve_extension_name(ext));
		}
		return SIEVE_EXEC_FAILURE;
	}
	return SIEVE_EXEC_OK;
}
