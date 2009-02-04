/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension imap4flags
 * --------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5232
 * Implementation: full 
 * Status: experimental, roughly tested
 *
 */
 
#include "lib.h"
#include "mempool.h"
#include "str.h"

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imap4flags-common.h"

/* 
 * Operations 
 */

const struct sieve_operation *imapflags_operations[] = { 
	&setflag_operation, 
	&addflag_operation, 
	&removeflag_operation,
	&hasflag_operation 
};

/* 
 * Extension
 */

static bool ext_imap4flags_validator_load(struct sieve_validator *valdtr);
static bool ext_imap4flags_interpreter_load
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

int ext_imap4flags_my_id = -1;

const struct sieve_extension imapflags_extension = { 
	"imap4flags", 
	&ext_imap4flags_my_id,
	NULL, NULL,
	ext_imap4flags_validator_load, 
	NULL, 
	ext_imap4flags_interpreter_load, 
	NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_OPERATIONS(imapflags_operations), 
	SIEVE_EXT_DEFINE_OPERAND(flags_side_effect_operand)
};

static bool ext_imap4flags_validator_load
(struct sieve_validator *valdtr)
{
	/* Register commands */
	sieve_validator_register_command(valdtr, &cmd_setflag);
	sieve_validator_register_command(valdtr, &cmd_addflag);
	sieve_validator_register_command(valdtr, &cmd_removeflag);
	sieve_validator_register_command(valdtr, &tst_hasflag);
	
	ext_imap4flags_attach_flags_tag(valdtr, "keep");
	ext_imap4flags_attach_flags_tag(valdtr, "fileinto");

	return TRUE;
}

static bool ext_imap4flags_interpreter_load
(const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{
	sieve_interpreter_extension_register
        (renv->interp, &imapflags_interpreter_extension, NULL);

	return TRUE;
}



