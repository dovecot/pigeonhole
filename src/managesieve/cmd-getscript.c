/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "ostream.h"
#include "istream.h"
#include "iostream.h"

#include "sieve-script.h"
#include "sieve-storage.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"

struct cmd_getscript_context {
	struct client *client;
	struct client_command_context *cmd;
	struct sieve_storage *storage;
	uoff_t script_size;

	const char *scriptname;
	struct sieve_script *script;
	struct istream *script_stream;

	bool failed:1;
};

static bool cmd_getscript_finish(struct cmd_getscript_context *ctx)
{
	struct client_command_context *cmd = ctx->cmd;
	struct client *client = ctx->client;

	sieve_script_unref(&ctx->script);

	if (ctx->failed) {
		if (client->output->closed) {
			client_disconnect(client, NULL);
			return TRUE;
		}

		client_command_storage_error(
			cmd, "Failed to retrieve script '%s'", ctx->scriptname);
		return TRUE;
	}

	client->get_count++;
	client->get_bytes += ctx->script_size;

	struct event_passthrough *e =
		client_command_create_finish_event(cmd)->
		add_int("script_size", ctx->script_size);
	e_debug(e->event(), "Retrieved script '%s'", ctx->scriptname);

	client_send_line(client, "");
	client_send_ok(client, "Getscript completed.");
	return TRUE;
}

static bool cmd_getscript_continue(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct cmd_getscript_context *ctx = cmd->context;

	switch (o_stream_send_istream(client->output, ctx->script_stream)) {
	case OSTREAM_SEND_ISTREAM_RESULT_FINISHED:
		if (ctx->script_stream->v_offset != ctx->script_size &&
		    !ctx->failed) {
			/* Input stream gave less data than expected */
			sieve_storage_set_critical(
				ctx->storage, "GETSCRIPT for script '%s' "
				"got too little data: "
				"%"PRIuUOFF_T" vs %"PRIuUOFF_T,
				sieve_script_label(ctx->script),
				ctx->script_stream->v_offset, ctx->script_size);
			client_disconnect(ctx->client, "GETSCRIPT failed");
			ctx->failed = TRUE;
		}
		break;
	case OSTREAM_SEND_ISTREAM_RESULT_WAIT_INPUT:
		i_unreached();
	case OSTREAM_SEND_ISTREAM_RESULT_WAIT_OUTPUT:
		return FALSE;
	case OSTREAM_SEND_ISTREAM_RESULT_ERROR_INPUT:
		sieve_storage_set_critical(ctx->storage,
			"o_stream_send_istream() failed for script '%s': "
			"%s",
			sieve_script_label(ctx->script),
			i_stream_get_error(ctx->script_stream));
		ctx->failed = TRUE;
		break;
	case OSTREAM_SEND_ISTREAM_RESULT_ERROR_OUTPUT:
		client_disconnect(ctx->client, NULL);
		ctx->failed = TRUE;
		break;
	}
	return cmd_getscript_finish(ctx);
}

bool cmd_getscript(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct cmd_getscript_context *ctx;
	const char *scriptname;
	enum sieve_error error_code;

	/* <scriptname> */
	if (!client_read_string_args(cmd, TRUE, 1, &scriptname))
		return FALSE;

	event_add_str(cmd->event, "script_name", scriptname);

	ctx = p_new(cmd->pool, struct cmd_getscript_context, 1);
	ctx->cmd = cmd;
	ctx->client = client;
	ctx->scriptname = p_strdup(cmd->pool, scriptname);
	ctx->storage = client->storage;
	ctx->failed = FALSE;

	if (sieve_storage_open_script(client->storage, scriptname,
				      &ctx->script, NULL) < 0) {
		ctx->failed = TRUE;
		return cmd_getscript_finish(ctx);
	}

	if (sieve_script_get_stream(ctx->script, &ctx->script_stream,
				    &error_code) < 0) {
		if (error_code == SIEVE_ERROR_NOT_FOUND) {
			sieve_storage_set_error(client->storage, error_code,
						"Script does not exist.");
		}
		ctx->failed = TRUE;
		return cmd_getscript_finish(ctx);
	}

	if (sieve_script_get_size(ctx->script, &ctx->script_size) <= 0) {
		sieve_storage_set_critical(ctx->storage,
			"failed to obtain script size for script '%s'",
			sieve_script_label(ctx->script));
		ctx->failed = TRUE;
		return cmd_getscript_finish(ctx);
	}

	i_assert(ctx->script_stream->v_offset == 0);

	client_send_line(client, t_strdup_printf("{%"PRIuUOFF_T"}",
						 ctx->script_size));

	client->command_pending = TRUE;
	cmd->func = cmd_getscript_continue;
	cmd->context = ctx;

	return cmd_getscript_continue(cmd);
}
