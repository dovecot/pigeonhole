/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

/* Extension mailbox
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5490
 * Implementation: almost full; acl support is missing for mailboxexists
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

#include "ext-mailbox-common.h"

/*
 * Extension
 */

static bool ext_mailbox_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr);

const struct sieve_extension_def mailbox_extension = {
	.name = "mailbox",
	.validator_load = ext_mailbox_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(mailboxexists_operation),
	SIEVE_EXT_DEFINE_OPERAND(mailbox_create_operand)
};

static bool ext_mailbox_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	/* Register :create tag with fileinto command and we don't care whether this
	 * command is registered or even whether it will be registered at all. The
	 * validator handles either situation gracefully
	 */
	sieve_validator_register_external_tag
		(valdtr, "fileinto", ext, &mailbox_create_tag, SIEVE_OPT_SIDE_EFFECT);

	/* Register new test */
	sieve_validator_register_command(valdtr, ext, &mailboxexists_test);

	return TRUE;
}


