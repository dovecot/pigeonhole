/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

/* Extension variables
 * -------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5183
 * Implementation: full
 * Status: testing
 *
 */

#include "lib.h"
#include "str.h"
#include "unichar.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"

#include "sieve-validator.h"

#include "ext-environment-common.h"

/*
 * Extension
 */

static bool ext_environment_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);
static bool ext_environment_interpreter_load
(const struct sieve_extension *ext,
	const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_extension_def environment_extension = {
	.name = "environment",
	.validator_load = ext_environment_validator_load,
	.interpreter_load = ext_environment_interpreter_load,
	SIEVE_EXT_DEFINE_OPERATION(tst_environment_operation)
};

static bool ext_environment_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	sieve_validator_register_command(valdtr, ext, &tst_environment);
	return TRUE;
}

static bool ext_environment_interpreter_load
(const struct sieve_extension *ext,
	const struct sieve_runtime_env *renv,
	sieve_size_t *address ATTR_UNUSED)
{
	ext_environment_interpreter_init(ext, renv->interp);
	return TRUE;
}

