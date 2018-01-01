/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension index
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5260
 * Implementation: full
 * Status: testing
 *
 */

#include "lib.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-message.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-index-common.h"

/*
 * Extension
 */

static bool ext_index_validator_load
(const struct sieve_extension *ext, struct sieve_validator *validator);

const struct sieve_extension_def index_extension = {
	.name = "index",
	.validator_load = ext_index_validator_load,
	SIEVE_EXT_DEFINE_OPERAND(index_operand)
};

static bool ext_index_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register :index and :last tags with header, address and date test commands
	 * and we don't care whether these command are registered or even whether
	 * these will be registered at all. The validator handles either situation
	 * gracefully.
	 */
	sieve_validator_register_external_tag
		(valdtr, "header", ext, &index_tag, SIEVE_OPT_MESSAGE_OVERRIDE);
	sieve_validator_register_external_tag
		(valdtr, "header", ext, &last_tag, 0);

	sieve_validator_register_external_tag
		(valdtr, "address", ext, &index_tag, SIEVE_OPT_MESSAGE_OVERRIDE);
	sieve_validator_register_external_tag
		(valdtr, "address", ext, &last_tag, 0);

	sieve_validator_register_external_tag
		(valdtr, "date", ext, &index_tag, SIEVE_OPT_MESSAGE_OVERRIDE);
	sieve_validator_register_external_tag
		(valdtr, "date", ext, &last_tag, 0);

	return TRUE;
}


