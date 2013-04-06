/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

/* Extension debug
 * ---------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5293
 * Implementation: partial
 * Status: experimental
 *
 */

#include "lib.h"
#include "array.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-editheader-common.h"

/*
 * Operations
 */

const struct sieve_operation_def *editheader_operations[] = {
	&addheader_operation,
	&deleteheader_operation
};

/*
 * Extension
 */

static bool ext_editheader_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *validator);

const struct sieve_extension_def editheader_extension = {
	.name = "editheader",
	.load = ext_editheader_load,
	.unload = ext_editheader_unload,
	.validator_load = ext_editheader_validator_load,
	SIEVE_EXT_DEFINE_OPERATIONS(editheader_operations)
};

static bool ext_editheader_validator_load
(const struct sieve_extension *ext, struct sieve_validator *validator)
{
	/* Register new commands */
	sieve_validator_register_command(validator, ext, &addheader_command);
	sieve_validator_register_command(validator, ext, &deleteheader_command);

	return TRUE;
}



