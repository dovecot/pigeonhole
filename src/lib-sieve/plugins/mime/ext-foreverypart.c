/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

/* Extension foreverypart
 * ----------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5703, Section 3
 * Implementation: full
 * Status: experimental
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

#include "ext-mime-common.h"

/*
 * Operations
 */

const struct sieve_operation_def *ext_foreverypart_operations[] = {
	&foreverypart_begin_operation,
	&foreverypart_end_operation,
	&break_operation
};

/*
 * Extension
 */

static bool ext_foreverypart_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);

const struct sieve_extension_def foreverypart_extension = {
	.name = "foreverypart",
	.validator_load = ext_foreverypart_validator_load,
	SIEVE_EXT_DEFINE_OPERATIONS(ext_foreverypart_operations)
};

/*
 * Extension validation
 */

static bool ext_foreverypart_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register new commands */
	sieve_validator_register_command(valdtr, ext, &cmd_foreverypart);
	sieve_validator_register_command(valdtr, ext, &cmd_break);

	return TRUE;
}
