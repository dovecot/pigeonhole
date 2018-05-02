/* Copyright (c) 2017-2018 Pigeonhole authors, see the included COPYING file */

#include "imap-common.h"

#include "imap-filter.h"
#include "imap-filter-sieve.h"

static bool
cmd_filter_parse_spec(struct imap_filter_context *ctx,
		       const struct imap_arg **_args)
{
	const struct imap_arg *args = *_args;
	struct client_command_context *cmd = ctx->cmd;
	const char *filter_type;

	/* filter-type */
	if (IMAP_ARG_IS_EOL(args)) {
		client_send_command_error(cmd,
			"Missing filter type.");
		return TRUE;
	}
	if (!imap_arg_get_atom(args, &filter_type)) {
		client_send_command_error(cmd,
			"Filter type is not an atom.");
		return TRUE;
	}
	if (strcasecmp(filter_type, "SIEVE") != 0) {
		client_send_command_error(cmd, t_strdup_printf(
			"Unknown filter type `%s'", filter_type));
		return TRUE;
	}

	cmd->func = cmd_filter_sieve;
	cmd->context = ctx;
	return cmd_filter_sieve(cmd);
}

bool cmd_filter(struct client_command_context *cmd)
{
	struct imap_filter_context *ctx;
	const struct imap_arg *args;

	if (!client_read_args(cmd, 1, 0, &args))
		return FALSE;

	if (!client_verify_open_mailbox(cmd))
		return TRUE;

	ctx = p_new(cmd->pool, struct imap_filter_context, 1);
	ctx->cmd = cmd;

	if (!cmd_filter_parse_spec(ctx, &args))
		return FALSE;

	imap_filter_context_free(ctx);
	return TRUE;
}
