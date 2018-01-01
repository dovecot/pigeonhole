/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension imapflags
 * --------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-melnikov-sieve-imapflags-03.txt
 * Implementation: full, but deprecated; provided for backwards compatibility
 * Status: testing
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
	(struct sieve_validator *valdtr, struct sieve_command *cmd);

/* Mark command
 *
 * Syntax:
 *   mark
 */

static const struct sieve_command_def cmd_mark = {
	.identifier = "mark",
	.type = SCT_COMMAND,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_mark_validate
};

/* Unmark command
 *
 * Syntax:
 *   unmark
 */
static const struct sieve_command_def cmd_unmark = {
	.identifier = "unmark",
	.type = SCT_COMMAND,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_mark_validate
};

/*
 * Extension
 */

static bool ext_imapflags_load
	(const struct sieve_extension *ext, void **context);
static bool ext_imapflags_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);
static bool ext_imapflags_interpreter_load
	(const struct sieve_extension *ext, const struct sieve_runtime_env *renv,
		sieve_size_t *address);

const struct sieve_extension_def imapflags_extension = {
	.name = "imapflags",
	.load = ext_imapflags_load,
	.validator_load = ext_imapflags_validator_load,
	.interpreter_load = ext_imapflags_interpreter_load
};

static bool ext_imapflags_load
(const struct sieve_extension *ext, void **context)
{
	if ( *context == NULL ) {
		/* Make sure real extension is registered, it is needed by the binary */
		*context = (void *)
			sieve_extension_require(ext->svinst, &imap4flags_extension, FALSE);
	}

	return TRUE;
}

/*
 * Validator
 */

static bool ext_imapflags_validator_check_conflict
	(const struct sieve_extension *ext,
		struct sieve_validator *valdtr, void *context,
		struct sieve_ast_argument *require_arg,
		const struct sieve_extension *other_ext,
		bool required);
static bool ext_imapflags_validator_validate
	(const struct sieve_extension *ext,
		struct sieve_validator *valdtr, void *context,
		struct sieve_ast_argument *require_arg,
		bool required);

const struct sieve_validator_extension
imapflags_validator_extension = {
	.ext = &imapflags_extension,
	.check_conflict = ext_imapflags_validator_check_conflict,
	.validate = ext_imapflags_validator_validate
};

static bool ext_imapflags_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	sieve_validator_extension_register
		(valdtr, ext, &imapflags_validator_extension, NULL);

	return TRUE;
}

static bool ext_imapflags_validator_check_conflict
(const struct sieve_extension *ext,
	struct sieve_validator *valdtr, void *context ATTR_UNUSED,
	struct sieve_ast_argument *require_arg,
	const struct sieve_extension *ext_other,
	bool required ATTR_UNUSED)
{
	const struct sieve_extension *master_ext =
		(const struct sieve_extension *) ext->context;

	if ( ext_other == master_ext ) {
		sieve_argument_validate_error(valdtr, require_arg,
			"the (deprecated) imapflags extension cannot be used "
			"together with the imap4flags extension");
		return FALSE;
	}

	return TRUE;
}

static bool ext_imapflags_validator_validate
(const struct sieve_extension *ext,
	struct sieve_validator *valdtr, void *context ATTR_UNUSED,
	struct sieve_ast_argument *require_arg ATTR_UNUSED,
	bool required ATTR_UNUSED)
{
	const struct sieve_extension *master_ext =
		(const struct sieve_extension *) ext->context;

	/* No conflicts */

	/* Register commands */
	sieve_validator_register_command(valdtr, master_ext, &cmd_setflag);
	sieve_validator_register_command(valdtr, master_ext, &cmd_addflag);
	sieve_validator_register_command(valdtr, master_ext, &cmd_removeflag);

	sieve_validator_register_command(valdtr, master_ext, &cmd_mark);
	sieve_validator_register_command(valdtr, master_ext, &cmd_unmark);

	/* Attach flags side-effect to keep and fileinto actions */
	sieve_ext_imap4flags_register_side_effect(valdtr, master_ext, "keep");
	sieve_ext_imap4flags_register_side_effect(valdtr, master_ext, "fileinto");

	return TRUE;
}

/*
 * Interpreter
 */

static bool ext_imapflags_interpreter_load
(const struct sieve_extension *ext, const struct sieve_runtime_env *renv,
	sieve_size_t *address ATTR_UNUSED)
{
	const struct sieve_extension *master_ext =
		(const struct sieve_extension *) ext->context;

	sieve_ext_imap4flags_interpreter_load(master_ext, renv);
	return TRUE;
}

/*
 * Command validation
 */

static bool cmd_mark_validate
(struct sieve_validator *valdtr, struct sieve_command *cmd)
{
	if ( sieve_command_is(cmd, cmd_mark) )
		cmd->def = &cmd_addflag;
	else
		cmd->def = &cmd_removeflag;

	cmd->first_positional = sieve_ast_argument_cstring_create
		(cmd->ast_node, "\\flagged", cmd->ast_node->source_line);

	if ( !sieve_validator_argument_activate
		(valdtr, cmd, cmd->first_positional, FALSE) )
		return FALSE;

	return TRUE;
}
