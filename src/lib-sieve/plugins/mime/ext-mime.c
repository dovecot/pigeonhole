/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

/* Extension mime
 * --------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5703, Section 4
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
#include "sieve-message.h"
#include "sieve-result.h"

#include "ext-mime-common.h"

/*
 * Extension
 */

static bool ext_mime_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);

const struct sieve_extension_def mime_extension = {
	.name = "mime",
	.validator_load = ext_mime_validator_load,
	SIEVE_EXT_DEFINE_OPERAND(mime_operand)
};

/*
 * Extension validation
 */

static bool ext_mime_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register :mime tag and friends with header, address and exists test
	 * commands and we don't care whether these command are registered or
	 * even whether these will be registered at all. The validator handles
	 * either situation gracefully.
	 */
	sieve_validator_register_external_tag
		(valdtr, "header", ext, &mime_tag, SIEVE_OPT_MESSAGE_OVERRIDE);
	sieve_validator_register_external_tag
		(valdtr, "header", ext, &mime_anychild_tag, 0);
	sieve_validator_register_external_tag
		(valdtr, "header", ext, &mime_type_tag, 0);
	sieve_validator_register_external_tag
		(valdtr, "header", ext, &mime_subtype_tag, 0);
	sieve_validator_register_external_tag
		(valdtr, "header", ext, &mime_contenttype_tag, 0);
	sieve_validator_register_external_tag
		(valdtr, "header", ext, &mime_param_tag, 0);

	sieve_validator_register_external_tag
		(valdtr, "address", ext, &mime_tag, SIEVE_OPT_MESSAGE_OVERRIDE);
	sieve_validator_register_external_tag
		(valdtr, "address", ext, &mime_anychild_tag, 0);

	sieve_validator_register_external_tag
		(valdtr, "exists", ext, &mime_tag, SIEVE_OPT_MESSAGE_OVERRIDE);
	sieve_validator_register_external_tag
		(valdtr, "exists", ext, &mime_anychild_tag, 0);

	return TRUE;
}
