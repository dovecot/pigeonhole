/* Copyright (c) 2025 Pigeonhole authors, see the included COPYING file
 */

/* Extension extlists
   --------------------

   Author: Stephan Bosch
   Specification: RFC 6134
   Implementation: :list match only
 */

#include "lib.h"
#include "str.h"

#include "sieve-common.h"

#include "sieve-ast.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-extlists-common.h"

/*
 * Extension
 */

static bool
ext_extlists_validator_load(const struct sieve_extension *ext,
			    struct sieve_validator *valdtr);

const struct sieve_extension_def extlists_extension = {
	.name = "extlists",
	.load = ext_extlists_load,
	.unload = ext_extlists_unload,
	.validator_load = ext_extlists_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(valid_ext_list_operation),
	SIEVE_EXT_DEFINE_OPERAND(list_match_type_operand),
};

static bool
ext_extlists_validator_load(const struct sieve_extension *ext,
			    struct sieve_validator *valdtr)
{
	/* Register new commands */
	sieve_validator_register_command(valdtr, ext, &valid_ext_list_test);

	/* Register :list tag with redirect command and we don't care
	   whether this command is registered or even whether it will be
	   registered at all. The validator handles either situation gracefully.
	 */
	sieve_validator_register_external_tag(
		valdtr, "redirect", ext, &redirect_list_tag, 0);

	sieve_match_type_register(valdtr, ext, &list_match_type);
	return TRUE;
}
