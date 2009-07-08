/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension notify
 * ----------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-notify-00.txt
 * Implementation: deprecated; provided for backwards compatibility
 *                 denotify command is explicitly not supported.
 * Status: deprecated
 * 
 */
	
#include <stdio.h>

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "ext-enotify-common.h"

/*
 * Commands
 */

static bool cmd_notify_registered
	(struct sieve_validator *valdtr,
		struct sieve_command_registration *cmd_reg);
static bool cmd_notify_pre_validate
	(struct sieve_validator *valdtr, struct sieve_command_context *cmd);
static bool cmd_notify_validate
	(struct sieve_validator *valdtr, struct sieve_command_context *cmd);
static bool cmd_notify_generate
	(const struct sieve_codegen_env *cgenv, 
		struct sieve_command_context *ctx);

static bool cmd_denotify_pre_validate
	(struct sieve_validator *valdtr, struct sieve_command_context *cmd);

/* Notify command
 *
 * Syntax:
 *   notify [":method" string] [":id" string] 
 *          [<":low" / ":normal" / ":high">] [":message" string]
 */

static const struct sieve_command cmd_notify = {
	"notify",
	SCT_COMMAND,
	0, 0, FALSE, FALSE,
	cmd_notify_registered,
	cmd_notify_pre_validate,
	cmd_notify_validate,
	cmd_notify_generate, 
	NULL,
};

/* Denotify command (NOT IMPLEMENTED)
 *
 * Syntax:
 *   denotify [MATCH-TYPE string] [<":low" / ":normal" / ":high">]
 */
static const struct sieve_command cmd_denotify = {
	"denotify",
	SCT_COMMAND,
	0, 0, FALSE, FALSE,
	NULL,
	cmd_denotify_pre_validate,
	NULL, NULL, NULL
};

/* 
 * Extension
 */

static bool ext_notify_load(void);
static bool ext_notify_validator_load(struct sieve_validator *valdtr);

static int ext_notify_my_id = -1;

const struct sieve_extension notify_extension = { 
	"notify", 
	&ext_notify_my_id,
	ext_notify_load,
	NULL,
	ext_notify_validator_load, 
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS,
	SIEVE_EXT_DEFINE_NO_OPERANDS,
};

static bool ext_notify_load(void)
{
	/* Make sure real extension is registered, it is needed by the binary */
    (void)sieve_extension_require(&enotify_extension);

	return TRUE;
}

/*
 * Extension validation
 */

static bool ext_notify_validator_extension_validate
	(struct sieve_validator *valdtr, void *context, struct sieve_ast_argument *require_arg);

const struct sieve_validator_extension notify_validator_extension = {
	&notify_extension,
	ext_notify_validator_extension_validate,
	NULL
};

static bool ext_notify_validator_load(struct sieve_validator *valdtr)
{
	/* Register validator extension to check for conflict with enotify */
	sieve_validator_extension_register
		(valdtr, &notify_validator_extension, NULL);

	/* Register new commands */
	sieve_validator_register_command(valdtr, &cmd_notify);
	sieve_validator_register_command(valdtr, &cmd_denotify);
	
	return TRUE;
}

static bool ext_notify_validator_extension_validate
(struct sieve_validator *valdtr, void *context ATTR_UNUSED,
    struct sieve_ast_argument *require_arg)
{
	/* Check for conflict with enotify */
	if ( sieve_validator_extension_loaded(valdtr, &enotify_extension) ) {
		sieve_argument_validate_error(valdtr, require_arg,
			"the (deprecated) notify extension cannot be used "
			"together with the enotify extension");
		return FALSE;
	}

	return TRUE;
}

/*
 * Denotify/Notify command tags
 */

/* Forward declarations */

static bool cmd_notify_validate_string_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command_context *cmd);
static bool cmd_notify_validate_importance_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command_context *cmd);

/* Argument objects */

static const struct sieve_argument notify_method_tag = {
	"method",
	NULL, NULL,
	cmd_notify_validate_string_tag,
	NULL, NULL
};

static const struct sieve_argument notify_id_tag = {
	"id",
	NULL, NULL,
	cmd_notify_validate_string_tag,
	NULL, NULL
};

static const struct sieve_argument notify_message_tag = {
	"message",
	NULL, NULL,
	cmd_notify_validate_string_tag,
	NULL, NULL
};

static const struct sieve_argument notify_low_tag = {
	"low",
	NULL, NULL,
	cmd_notify_validate_importance_tag,
	NULL, NULL
};

static const struct sieve_argument notify_normal_tag = {
	"normal",
	NULL, NULL,
	cmd_notify_validate_importance_tag,
	NULL, NULL
};

static const struct sieve_argument notify_high_tag = {
	"high",
	NULL, NULL,
	cmd_notify_validate_importance_tag,
	NULL, NULL
};

/*
 * Command validation context
 */

struct cmd_notify_context_data {
	struct sieve_ast_argument *method;
	struct sieve_ast_argument *message;
};

/*
 * Tag validation
 */

static bool cmd_notify_validate_string_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
    struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct cmd_notify_context_data *ctx_data =
		(struct cmd_notify_context_data *) cmd->data;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg, 1);

    /* Check syntax:
     *   :id <string>
     *   :method <string>
     *   :message <string>
     */
	if ( !sieve_validate_tag_parameter(valdtr, cmd, tag, *arg, SAAT_STRING) )
		return FALSE;

	if ( tag->argument == &notify_method_tag ) {
		ctx_data->method = *arg;
	
		/* Removed */
		*arg = sieve_ast_arguments_detach(*arg, 1);

	} else if ( tag->argument == &notify_id_tag ) {

		/* Ignored */
		*arg = sieve_ast_arguments_detach(*arg, 1);

	} else if ( tag->argument == &notify_message_tag ) {
		ctx_data->message = *arg;

		/* Skip parameter */
		*arg = sieve_ast_argument_next(*arg);
	}

	return TRUE;
}

static bool cmd_notify_validate_importance_tag
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_ast_argument **arg,
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_ast_argument *tag = *arg;

	if ( tag->argument == &notify_low_tag )
		sieve_ast_argument_number_substitute(tag, 1);
	else if ( tag->argument == &notify_normal_tag )
		sieve_ast_argument_number_substitute(tag, 2);
	else
		sieve_ast_argument_number_substitute(tag, 3);

	tag->argument = &number_argument;

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/*
 * Command registration
 */

static bool cmd_notify_registered
(struct sieve_validator *valdtr, struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag
		(valdtr, cmd_reg, &notify_method_tag, 0);
	sieve_validator_register_tag
		(valdtr, cmd_reg, &notify_id_tag, 0);
	sieve_validator_register_tag
		(valdtr, cmd_reg, &notify_message_tag, CMD_NOTIFY_OPT_MESSAGE);

	sieve_validator_register_tag
		(valdtr, cmd_reg, &notify_low_tag, CMD_NOTIFY_OPT_IMPORTANCE);
	sieve_validator_register_tag
		(valdtr, cmd_reg, &notify_normal_tag, CMD_NOTIFY_OPT_IMPORTANCE);
	sieve_validator_register_tag
		(valdtr, cmd_reg, &notify_high_tag, CMD_NOTIFY_OPT_IMPORTANCE);

	return TRUE;
}

/*
 * Command validation
 */

/* Notify */

static bool cmd_notify_pre_validate
(struct sieve_validator *valdtr ATTR_UNUSED,
	struct sieve_command_context *cmd)
{
	struct cmd_notify_context_data *ctx_data;
	
	/* Assign context */
	ctx_data = p_new(sieve_command_pool(cmd),	
		struct cmd_notify_context_data, 1);
	cmd->data = ctx_data;

	return TRUE;
}

static bool cmd_notify_validate
(struct sieve_validator *valdtr, struct sieve_command_context *cmd)
{
	struct cmd_notify_context_data *ctx_data =
		(struct cmd_notify_context_data *) cmd->data;

	if ( ctx_data->method == NULL )	{
		sieve_command_validate_error(valdtr, cmd,
            "the notify command must have a ':method' argument "
			"(the deprecated notify extension is not fully implemented)");
        return FALSE;
	}

	if ( !sieve_ast_argument_attach(cmd->ast_node, ctx_data->method) ) {
		/* Very unlikely */
		sieve_command_validate_error(valdtr, cmd,
			"generate notify command; script is too complex");
		return FALSE;
	}

	return ext_enotify_compile_check_arguments
		(valdtr, ctx_data->method, ctx_data->message, NULL, NULL);
}

/* Denotify */

static bool cmd_denotify_pre_validate
(struct sieve_validator *valdtr, struct sieve_command_context *cmd)
{
	sieve_command_validate_error(valdtr, cmd,
		"the denotify command cannot be used "
		"(the deprecated notify extension is not fully implemented)");
	return FALSE;
}

/*
 * Code generation
 */

static bool cmd_notify_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
    sieve_operation_emit_code(cgenv->sbin, &notify_operation);

    /* Emit source line */
    sieve_code_source_line_emit(cgenv->sbin, sieve_command_source_line(ctx));

    /* Generate arguments */
    return sieve_generate_arguments(cgenv, ctx, NULL);
}

