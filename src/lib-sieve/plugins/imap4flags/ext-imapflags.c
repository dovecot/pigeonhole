/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension imapflags
 * --------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-melnikov-sieve-imapflags-03.txt
 * Implementation: deprecated; provided for backwards compatibility
 * Status: deprecated
 *
 */
 
#include "lib.h"
#include "mempool.h"
#include "str.h"

#include "sieve-common.h"

#include "sieve-ast.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imap4flags-common.h"

/*
 * Commands
 */

static bool cmd_mark_validate
	(struct sieve_validator *valdtr, struct sieve_command_context *cmd);

/* Mark command
 *
 * Syntax:
 *   mark
 */

static const struct sieve_command cmd_mark = {
    "mark",
    SCT_COMMAND,
    0, 0, FALSE, FALSE,
    NULL, NULL,
    cmd_mark_validate,
    NULL, NULL,
};

/* Unmark command
 *
 * Syntax:
 *   unmark
 */
static const struct sieve_command cmd_unmark = {
    "unmark",
    SCT_COMMAND,
    0, 0, FALSE, FALSE,
    NULL, NULL,
    cmd_mark_validate,
    NULL, NULL,
};

/* 
 * Extension
 */

static bool ext_imapflags_load(void);
static bool ext_imapflags_validator_load(struct sieve_validator *valdtr);
static bool ext_imapflags_interpreter_load
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

int ext_imapflags_my_id = -1;

const struct sieve_extension imapflags_extension = { 
	"imapflags", 
	&ext_imapflags_my_id,
	ext_imapflags_load, 
	NULL,
	ext_imapflags_validator_load, 
	NULL,
	ext_imapflags_interpreter_load, 
	NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_imapflags_load(void)
{
	/* Make sure real extension is registered, it is needed by the binary */
	(void)sieve_extension_require(&imap4flags_extension);

	return TRUE;
}

/*
 * Validator
 */

static bool ext_imapflags_validator_extension_validate
	(struct sieve_validator *valdtr, void *context, struct sieve_ast_argument *require_arg);

const struct sieve_validator_extension imapflags_validator_extension = {
	&imapflags_extension,
	ext_imapflags_validator_extension_validate,
	NULL
};

static bool ext_imapflags_validator_load
(struct sieve_validator *valdtr)
{
	sieve_validator_extension_register
	    (valdtr, &imapflags_validator_extension, NULL);

	/* Register commands */
	sieve_validator_register_command(valdtr, &cmd_setflag);
	sieve_validator_register_command(valdtr, &cmd_addflag);
	sieve_validator_register_command(valdtr, &cmd_removeflag);

	sieve_validator_register_command(valdtr, &cmd_mark);
	sieve_validator_register_command(valdtr, &cmd_unmark);	
	
	return TRUE;
}

static bool ext_imapflags_validator_extension_validate
(struct sieve_validator *valdtr, void *context ATTR_UNUSED, 
	struct sieve_ast_argument *require_arg)
{
	if ( sieve_validator_extension_loaded(valdtr, &imap4flags_extension) ) {
		sieve_argument_validate_error(valdtr, require_arg,
			"the (deprecated) imapflags extension cannot be used "
			"together with the imap4flags extension");
		return FALSE;
	}

	return TRUE;
}

/*
 * Interpreter
 */

static bool ext_imapflags_interpreter_load
(const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{
    sieve_interpreter_extension_register
        (renv->interp, &imap4flags_interpreter_extension, NULL);

    return TRUE;
}

/*
 * Command validation
 */ 

static bool cmd_mark_validate
(struct sieve_validator *valdtr, struct sieve_command_context *cmd)
{
	if ( cmd->command == &cmd_mark )
		cmd->command = &cmd_addflag;
	else
		cmd->command = &cmd_removeflag;

	cmd->first_positional = sieve_ast_argument_cstring_create
		(cmd->ast_node, "\\flagged", cmd->ast_node->source_line);

	if ( !sieve_validator_argument_activate(valdtr, cmd, cmd->first_positional, FALSE) )
        return FALSE;	
		
	return TRUE;
}
