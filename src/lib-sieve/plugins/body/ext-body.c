/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension body 
 * ------------------
 *
 * Authors: Stephan Bosch, original CMUSieve implementation by Timo Sirainen
 * Specification: RFC 5173
 * Implementation: full, but text body-transform implementation is simple
 * Status: experimental, largely untested
 *
 */
 
/* FIXME: 
 *
 * From RFC with respect to :text body transform:
 *
 * "Sophisticated implementations MAY strip mark-up from the text prior
 *  to matching, and MAY convert media types other than text to text
 *  prior to matching.
 *
 *  (For example, they may be able to convert proprietary text editor
 *  formats to text or apply optical character recognition algorithms to
 *  image data.)"
 *
 * We might want to do this in the future, i.e. we must evaluate whether this is 
 * feasible.
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

int ext_body_my_id;

static bool ext_body_load(int ext_id);
static bool ext_body_validator_load(struct sieve_validator *validator);

const struct sieve_extension body_extension = { 
	"body", 
	&ext_body_my_id,
	ext_body_load,
	ext_body_validator_load, 
	NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_OPERATION(body_operation), 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_body_load(int ext_id) 
{
	ext_body_my_id = ext_id;
	return TRUE;
}

static bool ext_body_validator_load(struct sieve_validator *validator)
{
	/* Register new test */
	sieve_validator_register_command(validator, &body_test);

	return TRUE;
}


