/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

/* Extension body
 * ------------------
 *
 * Authors: Stephan Bosch
 *          Original CMUSieve implementation by Timo Sirainen
 * Specification: RFC 5173
 * Implementation: full
 * Status: testing
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

#include "ext-body-common.h"

/*
 * Extension
 */

static bool ext_body_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr);

const struct sieve_extension_def body_extension = {
	.name = "body",
	.validator_load =	ext_body_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(body_operation)
};

static bool ext_body_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register new test */
	sieve_validator_register_command(valdtr, ext, &body_test);

	return TRUE;
}


