/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* NOTE: this file also contains the checkscript command due to its obvious
 * similarities.
 */

#include "lib.h"
#include "ioloop.h"
#include "istream.h"
#include "ostream.h"
#include "str.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "managesieve-parser.h"

#include "managesieve-common.h"
#include "managesieve-client.h"
#include "managesieve-commands.h"
#include "managesieve-quota.h"

#include <sys/time.h>

struct cmd_putscript_context {
	struct client *client;
	struct client_command_context *cmd;
	struct sieve_storage *storage;

	struct istream *input;

	const char *scriptname;
	uoff_t script_size, max_script_size;

	struct managesieve_parser *save_parser;
	struct sieve_storage_save_context *save_ctx;

	bool script_size_valid:1;
};

static void cmd_putscript_finish(struct cmd_putscript_context *ctx);
static bool cmd_putscript_continue_script(struct client_command_context *cmd);

static void client_input_putscript(struct client *client)
{
	struct client_command_context *cmd = &client->cmd;

	i_assert(!client->destroyed);

	client->last_input = ioloop_time;
	timeout_reset(client->to_idle);

	switch (i_stream_read(client->input)) {
	case -1:
		/* disconnected */
		cmd_putscript_finish(cmd->context);
		/* Reset command so that client_destroy() doesn't try to call
		   cmd_putscript_continue_script() anymore. */
		_client_reset_command(client);
		client_destroy(client, "Disconnected in PUTSCRIPT/CHECKSCRIPT");
		return;
	case -2:
		cmd_putscript_finish(cmd->context);
		if (client->command_pending) {
			/* uploaded script data, this is handled internally by
			   mailbox_save_continue() */
			break;
		}

		/* parameter word is longer than max. input buffer size.
		   this is most likely an error, so skip the new data
		   until newline is found. */
		client->input_skip_line = TRUE;

		client_send_command_error(cmd, "Too long argument.");
		cmd->param_error = TRUE;
		_client_reset_command(client);
		return;
	}

	if (cmd->func(cmd)) {
		/* command execution was finished. Note that if cmd_sync()
		   didn't finish, we didn't get here but the input handler
		   has already been moved. So don't do anything important
		   here..

		   reset command once again to reset cmd_sync()'s changes. */
		_client_reset_command(client);

		if (client->input_pending)
			client_input(client);
	}
}

static void cmd_putscript_finish(struct cmd_putscript_context *ctx)
{
	managesieve_parser_destroy(&ctx->save_parser);

	io_remove(&ctx->client->io);
	o_stream_set_flush_callback(ctx->client->output,
				    client_output, ctx->client);

	if (ctx->save_ctx != NULL) {
		ctx->client->input_skip_line = TRUE;
		sieve_storage_save_cancel(&ctx->save_ctx);
	}
}

static bool cmd_putscript_continue_cancel(struct client_command_context *cmd)
{
	struct cmd_putscript_context *ctx = cmd->context;
	size_t size;

	(void)i_stream_read(ctx->input);
	(void)i_stream_get_data(ctx->input, &size);
	i_stream_skip(ctx->input, size);

	if (cmd->client->input->closed || ctx->input->eof ||
	    ctx->input->v_offset == ctx->script_size) {
		cmd_putscript_finish(ctx);
		return TRUE;
	}
	return FALSE;
}

static bool cmd_putscript_cancel(struct cmd_putscript_context *ctx, bool skip)
{
	ctx->client->input_skip_line = TRUE;

	if (!skip) {
		cmd_putscript_finish(ctx);
		return TRUE;
	}

	/* we have to read the nonsynced literal so we don't treat the uploaded
	   script as commands. */
	ctx->client->command_pending = TRUE;
	ctx->cmd->func = cmd_putscript_continue_cancel;
	ctx->cmd->context = ctx;
	return cmd_putscript_continue_cancel(ctx->cmd);
}

static void cmd_putscript_storage_error(struct cmd_putscript_context *ctx)
{
	struct client_command_context *cmd = ctx->cmd;

	if (ctx->scriptname == NULL) {
		client_command_storage_error(cmd, "Failed to check script");
	} else {
		client_command_storage_error(cmd, "Failed to store script `%s'",
					     ctx->scriptname);
	}
}

static bool cmd_putscript_save(struct cmd_putscript_context *ctx)
{
	/* Commit to save only when this is a putscript command */
	if (ctx->scriptname == NULL)
		return TRUE;

	/* Check commit */
	if (sieve_storage_save_commit(&ctx->save_ctx) < 0) {
		cmd_putscript_storage_error(ctx);
		return FALSE;
	}
	return TRUE;
}

static void
cmd_putscript_finish_script(struct cmd_putscript_context *ctx,
			    struct sieve_script *script)
{
	struct client *client = ctx->client;
	struct client_command_context *cmd = ctx->cmd;
	struct sieve_error_handler *ehandler;
	enum sieve_compile_flags cpflags =
		SIEVE_COMPILE_FLAG_NOGLOBAL | SIEVE_COMPILE_FLAG_UPLOADED;
	struct sieve_binary *sbin;
	bool success = TRUE;
	enum sieve_error error;
	string_t *errors;

	/* Mark this as an activation when we are replacing the
	   active script */
	if (sieve_storage_save_will_activate(ctx->save_ctx))
		cpflags |= SIEVE_COMPILE_FLAG_ACTIVATED;

	/* Prepare error handler */
	errors = str_new(default_pool, 1024);
	ehandler = sieve_strbuf_ehandler_create(
		client->svinst, errors, TRUE,
		client->set->managesieve_max_compile_errors);

	/* Compile */
	sbin = sieve_compile_script(script, ehandler, cpflags, &error);
	if (sbin == NULL) {
		const char *errormsg = NULL, *action;

		if (error != SIEVE_ERROR_NOT_VALID) {
			errormsg = sieve_script_get_last_error(script, &error);
			if (error == SIEVE_ERROR_NONE)
				errormsg = NULL;
		}

		action = (ctx->scriptname != NULL ?
			  t_strdup_printf("store script `%s'",
					  ctx->scriptname) :
			  "check script");

		if (errormsg == NULL) {
			struct event_passthrough *e =
				client_command_create_finish_event(cmd)->
				add_str("error", "Compilation failed")->
				add_int("compile_errors",
					sieve_get_errors(ehandler))->
				add_int("compile_warnings",
					sieve_get_warnings(ehandler));
			e_debug(e->event(), "Failed to %s: "
				"Compilation failed (%u errors, %u warnings)",
				action, sieve_get_errors(ehandler),
				sieve_get_warnings(ehandler));

			client_send_no(client, str_c(errors));
		} else {
			struct event_passthrough *e =
				client_command_create_finish_event(cmd)->
				add_str("error", errormsg);
			e_debug(e->event(), "Failed to %s: %s",
				action, errormsg);

			client_send_no(client, errormsg);
		}

		success = FALSE;
	} else {
		sieve_close(&sbin);

		if (!cmd_putscript_save(ctx))
			success = FALSE;
	}

	/* Finish up */
	cmd_putscript_finish(ctx);

	/* Report result to user */
	if (success) {
		if (ctx->scriptname != NULL) {
			client->put_count++;
			client->put_bytes += ctx->script_size;
		} else {
			client->check_count++;
			client->check_bytes += ctx->script_size;
		}

		struct event_passthrough *e =
			client_command_create_finish_event(cmd)->
			add_int("compile_warnings",
				sieve_get_warnings(ehandler));
		if (ctx->scriptname != NULL) {
			e_debug(e->event(), "Stored script `%s' successfully "
				"(%u warnings)", ctx->scriptname,
				sieve_get_warnings(ehandler));
		} else {
			e_debug(e->event(), "Checked script successfully "
				"(%u warnings)", sieve_get_warnings(ehandler));
		}

		if (sieve_get_warnings(ehandler) > 0)
			client_send_okresp(client, "WARNINGS", str_c(errors));
		else if (ctx->scriptname != NULL)
			client_send_ok(client, "PUTSCRIPT completed.");
		else
			client_send_ok(client, "Script checked successfully.");
	}

	sieve_error_handler_unref(&ehandler);
	str_free(&errors);
}

static void cmd_putscript_handle_script(struct cmd_putscript_context *ctx)
{
	struct client_command_context *cmd = ctx->cmd;
	struct sieve_script *script;

	/* Obtain script object for uploaded script */
	script = sieve_storage_save_get_tempscript(ctx->save_ctx);

	/* Check result */
	if (script == NULL) {
		cmd_putscript_storage_error(ctx);
		cmd_putscript_finish(ctx);
		return;
	}

	/* If quoted string, the size was not known until now */
	if (!ctx->script_size_valid) {
		if (sieve_script_get_size(script, &ctx->script_size) < 0) {
			cmd_putscript_storage_error(ctx);
			cmd_putscript_finish(ctx);
			return;
		}
		ctx->script_size_valid = TRUE;

		/* Check quota; max size is already checked */
		if (ctx->scriptname != NULL &&
		    !managesieve_quota_check_all(cmd, ctx->scriptname,
						 ctx->script_size)) {
			cmd_putscript_finish(ctx);
			return;
		}
	}

	/* Try to compile and store the script */
	T_BEGIN {
		cmd_putscript_finish_script(ctx, script);
	} T_END;
}

static bool cmd_putscript_finish_parsing(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct cmd_putscript_context *ctx = cmd->context;
	const struct managesieve_arg *args;
	int ret;

	/* if error occurs, the CRLF is already read. */
	client->input_skip_line = FALSE;

	/* <script literal> */
	ret = managesieve_parser_read_args(ctx->save_parser, 0, 0, &args);
	if (ret == -1 || client->output->closed) {
		if (ctx->storage != NULL) {
			const char *msg;
			bool fatal ATTR_UNUSED;

			msg = managesieve_parser_get_error(
				ctx->save_parser, &fatal);
			client_send_command_error(cmd, msg);
		}
		cmd_putscript_finish(ctx);
		return TRUE;
	}
	if (ret < 0) {
		/* need more data */
		return FALSE;
	}

	if (MANAGESIEVE_ARG_IS_EOL(&args[0])) {
		/* Eat away the trailing CRLF */
		client->input_skip_line = TRUE;

		cmd_putscript_handle_script(ctx);
		return TRUE;
	}

	client_send_command_error(cmd, "Too many command arguments.");
	cmd_putscript_finish(ctx);
	return TRUE;
}

static bool cmd_putscript_continue_parsing(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct cmd_putscript_context *ctx = cmd->context;
	const struct managesieve_arg *args;
	int ret;

	/* if error occurs, the CRLF is already read. */
	client->input_skip_line = FALSE;

	/* <script literal> */
	ret = managesieve_parser_read_args(
		ctx->save_parser, 0, MANAGESIEVE_PARSE_FLAG_STRING_STREAM,
		&args);
	if (ret == -1 || client->output->closed) {
		cmd_putscript_finish(ctx);
		client_send_command_error(cmd, "Invalid arguments.");
		client->input_skip_line = TRUE;
		return TRUE;
	}
	if (ret < 0) {
		/* need more data */
		return FALSE;
	}

	/* Validate the script argument */
	if (!managesieve_arg_get_string_stream(args,&ctx->input)) {
		client_send_command_error(cmd, "Invalid arguments.");
		return cmd_putscript_cancel(ctx, FALSE);
	}

	if (i_stream_get_size(ctx->input, FALSE, &ctx->script_size) > 0) {
		ctx->script_size_valid = TRUE;

		/* Check quota */
		if (ctx->scriptname == NULL) {
			if (!managesieve_quota_check_validsize(
				cmd, ctx->script_size))
				return cmd_putscript_cancel(ctx, TRUE);
		} else {
			if (!managesieve_quota_check_all(
				cmd, ctx->scriptname, ctx->script_size))
				return cmd_putscript_cancel(ctx, TRUE);
		}

	} else {
		ctx->max_script_size =
			managesieve_quota_max_script_size(client);
	}

	/* save the script */
	ctx->save_ctx = sieve_storage_save_init(ctx->storage, ctx->scriptname,
						ctx->input);

	if (ctx->save_ctx == NULL) {
		/* save initialization failed */
		cmd_putscript_storage_error(ctx);
		return cmd_putscript_cancel(ctx, TRUE);
	}

	/* after literal comes CRLF, if we fail make sure we eat it away */
	client->input_skip_line = TRUE;

	client->command_pending = TRUE;
	cmd->func = cmd_putscript_continue_script;
	return cmd_putscript_continue_script(cmd);
}

static bool cmd_putscript_continue_script(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct cmd_putscript_context *ctx = cmd->context;
	size_t size;
	int ret;

	if (ctx->save_ctx != NULL) {
		for (;;) {
			i_assert(!ctx->script_size_valid ||
				 ctx->input->v_offset <= ctx->script_size);
			if (ctx->max_script_size > 0 &&
			    ctx->input->v_offset > ctx->max_script_size) {
				(void)managesieve_quota_check_validsize(
					cmd, ctx->input->v_offset);
				cmd_putscript_finish(ctx);
				return TRUE;
			}

			ret = i_stream_read(ctx->input);
			if ((ret != -1 || ctx->input->stream_errno != EINVAL ||
			     client->input->eof) &&
			    sieve_storage_save_continue(ctx->save_ctx) < 0) {
				/* we still have to finish reading the script
			   	  from client */
				sieve_storage_save_cancel(&ctx->save_ctx);
				break;
			}
			if (ret == -1 || ret == 0)
				break;
		}
	}

	if (ctx->save_ctx == NULL) {
		(void)i_stream_read(ctx->input);
		(void)i_stream_get_data(ctx->input, &size);
		i_stream_skip(ctx->input, size);
	}

	if (ctx->input->eof || client->input->closed) {
		bool failed = FALSE;
		bool all_written = FALSE;

		if (!ctx->script_size_valid) {
			if (!client->input->eof &&
			    ctx->input->stream_errno == EINVAL) {
				client_send_command_error(
					cmd, t_strdup_printf(
						"Invalid input: %s",
						i_stream_get_error(ctx->input)));
				client->input_skip_line = TRUE;
				failed = TRUE;
			}
			all_written = (ctx->input->eof &&
				       ctx->input->stream_errno == 0);

		} else {
			all_written = (ctx->input->v_offset == ctx->script_size);
		}

		/* finished */
		ctx->input = NULL;

		if (!failed) {
			if (ctx->save_ctx == NULL) {
				/* failed above */
				cmd_putscript_storage_error(ctx);
				failed = TRUE;
			} else if (!all_written) {
				/* client disconnected before it finished sending the
					 whole script. */
				failed = TRUE;
				sieve_storage_save_cancel(&ctx->save_ctx);
				client_disconnect(
					client,
					"EOF while appending in PUTSCRIPT/CHECKSCRIPT");
			} else if (sieve_storage_save_finish(ctx->save_ctx) < 0) {
				failed = TRUE;
				cmd_putscript_storage_error(ctx);
			} else {
				failed = client->input->closed;
			}
		}

		if (failed) {
			cmd_putscript_finish(ctx);
			return TRUE;
		}

		/* finish */
		client->command_pending = FALSE;
		managesieve_parser_reset(ctx->save_parser);
		cmd->func = cmd_putscript_finish_parsing;
		return cmd_putscript_finish_parsing(cmd);
	}

	return FALSE;
}

static bool
cmd_putscript_start(struct client_command_context *cmd, const char *scriptname)
{
	struct cmd_putscript_context *ctx;
	struct client *client = cmd->client;

	ctx = p_new(cmd->pool, struct cmd_putscript_context, 1);
	ctx->cmd = cmd;
	ctx->client = client;
	ctx->storage = client->storage;
	ctx->scriptname = scriptname;

	io_remove(&client->io);
	client->io = io_add(i_stream_get_fd(client->input), IO_READ,
			    client_input_putscript, client);
	/* putscript is special because we're only waiting on client input, not
	   client output, so disable the standard output handler until we're
	   finished */
	o_stream_unset_flush_callback(client->output);

	ctx->save_parser = managesieve_parser_create(
		client->input, client->set->managesieve_max_line_length);

	cmd->func = cmd_putscript_continue_parsing;
	cmd->context = ctx;
	return cmd_putscript_continue_parsing(cmd);

}

bool cmd_putscript(struct client_command_context *cmd)
{
	const char *scriptname;

	/* <scriptname> */
	if (!client_read_string_args(cmd, FALSE, 1, &scriptname))
		return FALSE;

	event_add_str(cmd->event, "script_name", scriptname);

	return cmd_putscript_start(cmd, scriptname);
}

bool cmd_checkscript(struct client_command_context *cmd)
{
	return cmd_putscript_start(cmd, NULL);
}
