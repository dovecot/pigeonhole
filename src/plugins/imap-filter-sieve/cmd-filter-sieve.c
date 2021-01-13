/* Copyright (c) 2017-2018 Pigeonhole authors, see the included COPYING file */

#include "imap-common.h"
#include "str.h"
#include "istream.h"
#include "istream-seekable.h"
#include "ostream.h"
#include "imap-commands.h"

#include "imap-filter.h"
#include "imap-filter-sieve.h"

#define FILTER_MAX_INMEM_SIZE (1024*128)

static int cmd_filter_sieve_compile_script(struct imap_filter_context *ctx)
{
	struct client_command_context *cmd = ctx->cmd;
	struct imap_filter_sieve_context *sctx = ctx->sieve;
	struct client *client = cmd->client;
	string_t *errors = NULL;
	bool have_warnings = FALSE;
	int ret = 0;

	ret = imap_filter_sieve_compile(sctx, &errors, &have_warnings);
	if (ret >= 0 && !have_warnings)
		return 0;
		
	o_stream_nsend_str(client->output,
		t_strdup_printf("* FILTER (TAG %s) "
				"%s {%zu}\r\n",
				cmd->tag, (ret < 0 ? "ERRORS" : "WARNINGS"),
				str_len(errors)));
	o_stream_nsend(client->output,
		       str_data(errors), str_len(errors));
	o_stream_nsend_str(client->output, "\r\n");
	
	if (ret < 0) {
		ctx->compile_failure = TRUE;
		ctx->failed = TRUE;
		return -1;
	}
	return 0;
}

static bool cmd_filter_sieve_delivery(struct client_command_context *cmd)
{
	struct imap_filter_context *ctx = cmd->context;
	struct client *client = cmd->client;
	struct imap_filter_sieve_context *sctx = ctx->sieve;
	enum mail_error error;
	const char *error_string;
	int ret;

	if (cmd->cancel) {
		imap_filter_deinit(ctx);
		return TRUE;
	}

	i_assert(sctx->filter_type == IMAP_FILTER_SIEVE_TYPE_DELIVERY);
	ret = imap_filter_sieve_open_personal(sctx, NULL,
					      &error, &error_string);
	if (ret < 0) {
		client_send_tagline(
			cmd, imap_get_error_string(cmd, error_string, error));
		imap_filter_deinit(ctx);
		return TRUE;
	}
	if (cmd_filter_sieve_compile_script(ctx) < 0) {
		client_send_tagline(cmd, "NO Failed to compile Sieve script");
		client->input_skip_line = TRUE;
		imap_filter_deinit(ctx);
		return TRUE;
	}

	imap_parser_reset(ctx->parser);
	cmd->func = imap_filter_search;
	return imap_filter_search(cmd);
}

static int
cmd_filter_sieve_script_parse_name_arg(struct imap_filter_context *ctx)
{
	struct client_command_context *cmd = ctx->cmd;
	const struct imap_arg *args;
	const char *error;
	enum imap_parser_error parse_error;
	int ret;

	ret = imap_parser_read_args(ctx->parser, 1, 0, &args);
	if (ret < 0) {
		if (ret == -2)
			return 0;
		error = imap_parser_get_error(ctx->parser, &parse_error);
		switch (parse_error) {
		case IMAP_PARSE_ERROR_NONE:
			i_unreached();
		case IMAP_PARSE_ERROR_LITERAL_TOO_BIG:
			client_disconnect_with_error(ctx->cmd->client, error);
			break;
		default:
			client_send_command_error(ctx->cmd, error);
			break;
		}
		return -1;
	}

	switch (args[0].type) {
	case IMAP_ARG_EOL:
		client_send_command_error(ctx->cmd, "Script name missing");
		return -1;
	case IMAP_ARG_NIL:
	case IMAP_ARG_LIST:
		client_send_command_error(
			ctx->cmd, "Script name must be an atom or a string");
		return -1;
	case IMAP_ARG_ATOM:
	case IMAP_ARG_STRING:
		/* We have the value already */
		if (ctx->failed)
			return 1;
		ctx->script_name = p_strdup(cmd->pool,
					    imap_arg_as_astring(&args[0]));
		break;
	case IMAP_ARG_LITERAL:
	case IMAP_ARG_LITERAL_SIZE:
	case IMAP_ARG_LITERAL_SIZE_NONSYNC:
		i_unreached();
	}
	return 1;
}

static bool
cmd_filter_sieve_script_parse_name(struct client_command_context *cmd)
{
	struct imap_filter_context *ctx = cmd->context;
	struct client *client = cmd->client;
	struct imap_filter_sieve_context *sctx = ctx->sieve;
	enum mail_error error;
	const char *error_string;
	int ret;

	if (cmd->cancel) {
		imap_filter_deinit(ctx);
		return TRUE;
	}

	if ((ret = cmd_filter_sieve_script_parse_name_arg(ctx)) == 0)
		return FALSE;
	if (ret < 0) {
		/* Already sent the error to client */
		imap_filter_deinit(ctx);
		return TRUE;
	}

	switch (sctx->filter_type) {
	case IMAP_FILTER_SIEVE_TYPE_PERSONAL:
		ret = imap_filter_sieve_open_personal(sctx, ctx->script_name,
						      &error, &error_string);
		break;
	case IMAP_FILTER_SIEVE_TYPE_GLOBAL:
		ret = imap_filter_sieve_open_global(sctx, ctx->script_name,
						    &error, &error_string);
		break;
	case IMAP_FILTER_SIEVE_TYPE_DELIVERY:
	case IMAP_FILTER_SIEVE_TYPE_SCRIPT:
		i_unreached();
	}
	if (ret < 0) {
		client_send_tagline(
			cmd, imap_get_error_string(cmd, error_string, error));
		imap_filter_deinit(ctx);
		return TRUE;
	}
	if (cmd_filter_sieve_compile_script(ctx) < 0) {
		client_send_tagline(cmd, "NO Failed to compile Sieve script");
		client->input_skip_line = TRUE;
		imap_filter_deinit(ctx);
		return TRUE;
	}

	imap_parser_reset(ctx->parser);
	cmd->func = imap_filter_search;
	return imap_filter_search(cmd);
}

static void
cmd_filter_sieve_compile_input(struct imap_filter_context *ctx,
				struct istream *input)
{
	struct imap_filter_sieve_context *sctx = ctx->sieve;

	imap_filter_sieve_open_input(sctx, input);
	(void)cmd_filter_sieve_compile_script(ctx);
}

static int cmd_filter_sieve_script_read_stream(struct imap_filter_context *ctx)
{
	struct istream *input = ctx->script_input;
	const unsigned char *data;
	size_t size;
	int ret;

	while ((ret = i_stream_read_more(input, &data, &size)) > 0)
		i_stream_skip(input, size);
	if (ret == 0)
		return 0;

	if (input->v_offset != ctx->script_len) {
		/* Client disconnected */
		i_assert(input->eof);
		return -1;
	}
	/* Finished reading the value */
	i_stream_seek(input, 0);

	if (ctx->failed) {
		i_stream_unref(&ctx->script_input);
		return 1;
	}

	cmd_filter_sieve_compile_input(ctx, ctx->script_input);
	i_stream_unref(&ctx->script_input);
	return 1;
}

static int
cmd_filter_sieve_script_parse_value_arg(struct imap_filter_context *ctx)
{
	const struct imap_arg *args;
	const char *value, *error;
	enum imap_parser_error parse_error;
	struct istream *input, *inputs[2];
	string_t *path;
	int ret;

	ret = imap_parser_read_args(ctx->parser, 1,
				    IMAP_PARSE_FLAG_LITERAL_SIZE |
				    IMAP_PARSE_FLAG_LITERAL8, &args);
	if (ret < 0) {
		if (ret == -2)
			return 0;
		error = imap_parser_get_error(ctx->parser, &parse_error);
		switch (parse_error) {
		case IMAP_PARSE_ERROR_NONE:
			i_unreached();
		case IMAP_PARSE_ERROR_LITERAL_TOO_BIG:
			client_disconnect_with_error(ctx->cmd->client, error);
			break;
		default:
			client_send_command_error(ctx->cmd, error);
			break;
		}
		return -1;
	}

	switch (args[0].type) {
	case IMAP_ARG_EOL:
		client_send_command_error(ctx->cmd, "Script value missing");
		return -1;
	case IMAP_ARG_NIL:
	case IMAP_ARG_ATOM:
	case IMAP_ARG_LIST:
		client_send_command_error(ctx->cmd,
					  "Script value must be a string");
		return -1;
	case IMAP_ARG_STRING:
		/* We have the value already */
		if (ctx->failed)
			return 1;
		value = imap_arg_as_astring(&args[0]);
		input = i_stream_create_from_data(value, strlen(value));
		cmd_filter_sieve_compile_input(ctx, input);
		i_stream_unref(&input);
		return 1;
	case IMAP_ARG_LITERAL_SIZE:
		o_stream_nsend(ctx->cmd->client->output, "+ OK\r\n", 6);
		o_stream_uncork(ctx->cmd->client->output);
		o_stream_cork(ctx->cmd->client->output);
		/* Fall through */
	case IMAP_ARG_LITERAL_SIZE_NONSYNC:
		ctx->script_len = imap_arg_as_literal_size(&args[0]);

		inputs[0] = i_stream_create_limit(ctx->cmd->client->input,
						  ctx->script_len);
		inputs[1] = NULL;

		path = t_str_new(128);
		mail_user_set_get_temp_prefix(path,
					      ctx->cmd->client->user->set);
		ctx->script_input = i_stream_create_seekable_path(
			inputs, FILTER_MAX_INMEM_SIZE, str_c(path));
		i_stream_set_name(ctx->script_input,
				  i_stream_get_name(inputs[0]));
		i_stream_unref(&inputs[0]);
		break;
	case IMAP_ARG_LITERAL:
		i_unreached();
	}
	return cmd_filter_sieve_script_read_stream(ctx);
}

static bool
cmd_filter_sieve_script_parse_value(struct client_command_context *cmd)
{
	struct imap_filter_context *ctx = cmd->context;
	struct client *client = cmd->client;
	int ret;

	if (cmd->cancel) {
		imap_filter_deinit(ctx);
		return TRUE;
	}

	if (ctx->script_input != NULL) {
		if ((ret = cmd_filter_sieve_script_read_stream(ctx)) == 0)
			return FALSE;
	} else {
		if ((ret = cmd_filter_sieve_script_parse_value_arg(ctx)) == 0)
			return FALSE;
	}

	if (ret < 0) {
		/* Already sent the error to client */ ;
		imap_filter_deinit(ctx);
		return TRUE;
	} else if (ctx->compile_failure) {
		client_send_tagline(cmd, "NO Failed to compile Sieve script");
		client->input_skip_line = TRUE;
		imap_filter_deinit(ctx);
		return TRUE;
	}

	imap_parser_reset(ctx->parser);
	cmd->func = imap_filter_search;
	return imap_filter_search(cmd);
}

bool cmd_filter_sieve(struct client_command_context *cmd)
{
	struct imap_filter_context *ctx = cmd->context;
	struct client *client = cmd->client;
	enum imap_filter_sieve_type type;
	const struct imap_arg *args;
	const char *sieve_type;

	if (!client_read_args(cmd, 2, 0, &args))
		return FALSE;
	args++;

	/* sieve-type */
	if (IMAP_ARG_IS_EOL(args)) {
		client_send_command_error(
			cmd, "Missing SIEVE filter sub-type.");
		return TRUE;
	}
	if (!imap_arg_get_atom(args, &sieve_type)) {
		client_send_command_error(
			cmd, "SIEVE filter sub-type is not an atom.");
		return TRUE;
	}
	if (strcasecmp(sieve_type, "DELIVERY") == 0) {
		type = IMAP_FILTER_SIEVE_TYPE_DELIVERY;
	} else if (strcasecmp(sieve_type, "PERSONAL") == 0) {
		type = IMAP_FILTER_SIEVE_TYPE_PERSONAL;
	} else if (strcasecmp(sieve_type, "GLOBAL") == 0) {
		type = IMAP_FILTER_SIEVE_TYPE_GLOBAL;
	} else if (strcasecmp(sieve_type, "SCRIPT") == 0) {
		type = IMAP_FILTER_SIEVE_TYPE_SCRIPT;
	} else {
		client_send_command_error(cmd, t_strdup_printf(
			"Unknown SIEVE filter sub-type `%s'",
			sieve_type));
		return TRUE;
	}

	ctx->sieve = imap_filter_sieve_context_create(ctx, type);

	/* We support large scripts, so read the values from client
	   asynchronously the same way as APPEND does. */
	client->input_lock = cmd;
	ctx->parser = imap_parser_create(client->input, client->output,
					 client->set->imap_max_line_length);
	if (client->set->imap_literal_minus)
		imap_parser_enable_literal_minus(ctx->parser);
	o_stream_unset_flush_callback(client->output);

	switch (type) {
	case IMAP_FILTER_SIEVE_TYPE_DELIVERY:
		cmd->func = cmd_filter_sieve_delivery;
		break;
	case IMAP_FILTER_SIEVE_TYPE_PERSONAL:
		cmd->func = cmd_filter_sieve_script_parse_name;
		break;
	case IMAP_FILTER_SIEVE_TYPE_GLOBAL:
		cmd->func = cmd_filter_sieve_script_parse_name;
		break;
	case IMAP_FILTER_SIEVE_TYPE_SCRIPT:
		cmd->func = cmd_filter_sieve_script_parse_value;
		break;
	}
	cmd->context = ctx;
	return cmd->func(cmd);
}
