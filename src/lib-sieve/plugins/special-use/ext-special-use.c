/* Copyright (c) 2019 Pigeonhole authors, see the included COPYING file */

/* Extension special-use
 * ---------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 8579
 * Implementation: full
 * Status: testing
 *
 */

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "ext-special-use-common.h"

/*
 * Extension
 */

static bool
ext_special_use_validator_load(const struct sieve_extension *ext,
			       struct sieve_validator *valdtr);

const struct sieve_extension_def special_use_extension = {
	.name = "special-use",
	.validator_load = ext_special_use_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(specialuse_exists_operation),
	SIEVE_EXT_DEFINE_OPERAND(specialuse_operand)
};

static bool
ext_special_use_validator_load(const struct sieve_extension *ext,
			       struct sieve_validator *valdtr)
{
	/* Register :specialuse tag with fileinto command and we don't care
	   whether this command is registered or even whether it will be
	   registered at all. The validator handles either situation gracefully.
	 */
	sieve_validator_register_external_tag(
		valdtr, "fileinto", ext, &specialuse_tag,
		SIEVE_OPT_SIDE_EFFECT);

	/* Register new test */
	sieve_validator_register_command(valdtr, ext, &specialuse_exists_test);

	return TRUE;
}
