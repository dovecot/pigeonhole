/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "login-common.h"
#include "buffer.h"
#include "ioloop.h"
#include "istream.h"
#include "ostream.h"
#include "safe-memset.h"
#include "str.h"
#include "strescape.h"
#include "base64.h"
#include "master-service.h"
#include "master-auth.h"
#include "auth-client.h"

#include "managesieve-parser.h"
#include "managesieve-quote.h"

#include "client.h"
#include "client-authenticate.h"

#include "managesieve-login-settings.h"
#include "managesieve-proxy.h"

/* Disconnect client when it sends too many bad commands */
#define CLIENT_MAX_BAD_COMMANDS 3

struct managesieve_command {
	const char *name;
	int (*func)(struct managesieve_client *client,
		    const struct managesieve_arg *args);
	int preparsed_args;
};

/* Skip incoming data until newline is found,
   returns TRUE if newline was found. */
bool client_skip_line(struct managesieve_client *client)
{
	const unsigned char *data;
	size_t i, data_size;

	data = i_stream_get_data(client->common.input, &data_size);

	for (i = 0; i < data_size; i++) {
		if (data[i] == '\n') {
			i_stream_skip(client->common.input, i+1);
			return TRUE;
		}
	}

	return FALSE;
}

static void client_send_capabilities(struct client *client)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *)client;
	const char *saslcap;

	T_BEGIN {
		saslcap = client_authenticate_get_capabilities(client);

		/* Default capabilities */
		client_send_raw(client, t_strconcat(
			"\"IMPLEMENTATION\" \"",
			msieve_client->set->managesieve_implementation_string,
			"\"\r\n", NULL));
		client_send_raw(client, t_strconcat(
			"\"SIEVE\" \"",
			msieve_client->set->managesieve_sieve_capability,
			"\"\r\n", NULL));
		if (msieve_client->set->managesieve_notify_capability != NULL) {
			client_send_raw(client, t_strconcat(
				"\"NOTIFY\" \"",
				msieve_client->set->managesieve_notify_capability,
				"\"\r\n", NULL));
		}
		client_send_raw(client, t_strconcat("\"SASL\" \"", saslcap,
						    "\"\r\n", NULL));

		/* STARTTLS */
		if (login_ssl_initialized && !client->tls)
			client_send_raw(client, "\"STARTTLS\"\r\n");

		/* Protocol version */
		client_send_raw(client, "\"VERSION\" \"1.0\"\r\n");

		/* XCLIENT */
		if (client->trusted)
			client_send_raw(client, "\"XCLIENT\"\r\n");
	} T_END;
}

static int
cmd_capability(struct managesieve_client *client,
	       const struct managesieve_arg *args ATTR_UNUSED)
{
	o_stream_cork(client->common.output);

	client_send_capabilities(&client->common);
	client_send_ok(&client->common, "Capability completed.");

	o_stream_uncork(client->common.output);

	return 1;
}

static int
cmd_starttls(struct managesieve_client *client,
	     const struct managesieve_arg *args ATTR_UNUSED)
{
	client_cmd_starttls(&client->common);
	return 1;
}

static void
managesieve_client_notify_starttls(struct client *client, bool success,
				   const char *text)
{
	if (success)
		client_send_ok(client, text);
	else
		client_send_no(client, text);
}

static int
cmd_noop(struct managesieve_client *client, const struct managesieve_arg *args)
{
	const char *text;
	string_t *resp_code;

	if (MANAGESIEVE_ARG_IS_EOL(&args[0])) {
		client_send_ok(&client->common, "NOOP Completed");
		return 1;
	}
	if (!MANAGESIEVE_ARG_IS_EOL(&args[1]))
		return -1;
	if (!managesieve_arg_get_string(&args[0], &text)) {
		client_send_no(&client->common, "Invalid echo tag.");
		return 1;
	}

	resp_code = t_str_new(256);
	str_append(resp_code, "TAG ");
	managesieve_quote_append_string(resp_code, text, FALSE);

	client_send_okresp(&client->common, str_c(resp_code), "Done");
	return 1;
}

static int
cmd_logout(struct managesieve_client *client,
	   const struct managesieve_arg *args ATTR_UNUSED)
{
	client_send_ok(&client->common, "Logout completed.");
	client_destroy(&client->common, CLIENT_UNAUTHENTICATED_LOGOUT_MSG);
	return 1;
}

static int
cmd_xclient_parse_forward(struct managesieve_client *client, const char *value)
{
	size_t value_len = strlen(value);

	if (client->common.forward_fields != NULL)
		str_truncate(client->common.forward_fields, 0);
	else {
		client->common.forward_fields =	str_new(
			client->common.preproxy_pool,
			MAX_BASE64_DECODED_SIZE(value_len));
	}

	if (base64_decode(value, value_len, NULL,
			  client->common.forward_fields) < 0)
		return -1;

	return 0;
}

static int
cmd_xclient(struct managesieve_client *client,
	    const struct managesieve_arg *args)
{
	const char *arg;
	bool args_ok = TRUE;

	if (!client->common.trusted) {
		client_send_no(&client->common, "You are not from trusted IP");
		return 1;
	}
	while (!MANAGESIEVE_ARG_IS_EOL(&args[0]) &&
		managesieve_arg_get_atom(&args[0], &arg)) {
		if (strncasecmp(arg, "ADDR=", 5) == 0) {
			if (net_addr2ip(arg + 5, &client->common.ip) < 0)
				args_ok = FALSE;
		} else if (strncasecmp(arg, "FORWARD=", 8)  == 0) {
			if (cmd_xclient_parse_forward(client, arg + 8) < 0)
				args_ok = FALSE;
		} else if (strncasecmp(arg, "PORT=", 5) == 0) {
			if (net_str2port(arg + 5,
					 &client->common.remote_port) < 0)
				args_ok = FALSE;
		} else if (strncasecmp(arg, "SESSION=", 8) == 0) {
			const char *value = arg + 8;

			if (strlen(value) <= LOGIN_MAX_SESSION_ID_LEN) {
				client->common.session_id =
					p_strdup(client->common.pool, value);
			}
		} else if (strncasecmp(arg, "TTL=", 4)  == 0) {
			if (str_to_uint(arg + 4, &client->common.proxy_ttl) < 0)
				args_ok = FALSE;
		}
		args++;
	}
	if (!args_ok || !MANAGESIEVE_ARG_IS_EOL(&args[0]))
		return -1;

	client_send_ok(&client->common, "Updated");
	return 1;
}

static struct managesieve_command commands[] = {
	{ "AUTHENTICATE", cmd_authenticate, 1 },
	{ "CAPABILITY", cmd_capability, -1 },
	{ "STARTTLS", cmd_starttls, -1 },
	{ "NOOP", cmd_noop, 0 },
	{ "LOGOUT", cmd_logout, -1 },
	{ "XCLIENT", cmd_xclient, 0 },
	{ NULL, NULL, 0 }
};

static bool client_handle_input(struct managesieve_client *client)
{
	i_assert(!client->common.authenticating);

	if (client->cmd_finished) {
		/* Clear the previous command from memory */
		client->cmd_name = NULL;
		client->cmd_parsed_args = FALSE;
		client->cmd = NULL;
		managesieve_parser_reset(client->parser);

		/* Remove \r\n */
		if (client->skip_line) {
			if (!client_skip_line(client))
				return FALSE;
			client->skip_line = FALSE;
		}

		client->cmd_finished = FALSE;
	}

	if (client->cmd == NULL) {
		struct managesieve_command *cmd;
		const char *cmd_name;

		client->cmd_name = managesieve_parser_read_word(client->parser);
		if (client->cmd_name == NULL)
			return FALSE; /* Need more data */

		cmd_name = t_str_ucase(client->cmd_name);
		cmd = commands;
		while (cmd->name != NULL) {
			if (strcmp(cmd->name, cmd_name) == 0)
				break;
			cmd++;
		}

		if (cmd->name != NULL)
			client->cmd = cmd;
		else
			client->skip_line = TRUE;
	}
	return client->common.v.input_next_cmd(&client->common);
}

static bool managesieve_client_input_next_cmd(struct client *_client)
{
	struct managesieve_client *client =
		(struct managesieve_client *)_client;
	const struct managesieve_arg *args = NULL;
	const char *msg;
	int ret = 1;
	bool fatal;

	if (client->cmd == NULL) {
		/* Unknown command */
		ret = -1;
	} else if (!client->cmd_parsed_args) {
		unsigned int arg_count =
			(client->cmd->preparsed_args > 0 ?
			 client->cmd->preparsed_args : 0);

		switch (managesieve_parser_read_args(client->parser, arg_count,
						     0, &args)) {
		case -1:
			/* Error */
			msg = managesieve_parser_get_error(client->parser,
							   &fatal);
			if (fatal) {
				client_send_bye(&client->common, msg);
				client_destroy(&client->common, msg);
				return FALSE;
			}
			client_send_no(&client->common, msg);
			client->cmd_finished = TRUE;
			client->skip_line = TRUE;
			return TRUE;
		case -2:
			/* Not enough data */
			return FALSE;
		}
		i_assert(args != NULL);

		if (arg_count == 0) {
			/* We read the entire line - skip over the CRLF */
			if (!client_skip_line(client))
				i_unreached();
		} else {
			/* Get rid of it later */
			client->skip_line = TRUE;
		}

		client->cmd_parsed_args = TRUE;

		if (client->cmd->preparsed_args == -1) {
			/* Check absence of arguments */
			if (args[0].type != MANAGESIEVE_ARG_EOL)
				ret = -1;
		}
	}
	if (ret > 0) {
		i_assert(client->cmd != NULL);
		ret = client->cmd->func(client, args);
	}

	if (ret != 0)
		client->cmd_finished = TRUE;
	if (ret < 0) {
		if (++client->common.bad_counter >= CLIENT_MAX_BAD_COMMANDS) {
			client_send_bye(&client->common,
				"Too many invalid MANAGESIEVE commands.");
			client_destroy(&client->common,
				       "Too many invalid commands.");
			return FALSE;
		}
		client_send_no(&client->common,
			"Error in MANAGESIEVE command received by server.");
	}

	return ret != 0 && !client->common.destroyed;
}

static void managesieve_client_input(struct client *client)
{
	struct managesieve_client *managesieve_client =
		(struct managesieve_client *)client;

	if (!client_read(client))
		return;

	client_ref(client);
	o_stream_cork(managesieve_client->common.output);
	for (;;) {
		if (!auth_client_is_connected(auth_client)) {
			/* We're not currently connected to auth process -
			   don't allow any commands */
			/* FIXME: Can't do untagged responses with managesieve.
			   Any other ways?
			   client_send_ok(client, AUTH_SERVER_WAITING_MSG);
			*/
			timeout_remove(&client->to_auth_waiting);

			client->input_blocked = TRUE;
			break;
		} else {
			if (!client_handle_input(managesieve_client))
				break;
		}
	}
	o_stream_uncork(managesieve_client->common.output);
	client_unref(&client);
}

static struct client *managesieve_client_alloc(pool_t pool)
{
	struct managesieve_client *msieve_client;

	msieve_client = p_new(pool, struct managesieve_client, 1);
	return &msieve_client->common;
}

static void managesieve_client_create(struct client *client, void **other_sets)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *)client;

	msieve_client->set = other_sets[0];
	msieve_client->parser = managesieve_parser_create(
		msieve_client->common.input, MAX_MANAGESIEVE_LINE);
	client->io = io_add(client->fd, IO_READ, client_input, client);
}

static void managesieve_client_destroy(struct client *client)
{
	struct managesieve_client *managesieve_client =
		(struct managesieve_client *)client;

	managesieve_parser_destroy(&managesieve_client->parser);
}

static void managesieve_client_notify_auth_ready(struct client *client)
{
	/* Cork the stream to send the capability data as a single tcp frame
	   Some naive clients break if we don't.
	 */
	o_stream_cork(client->output);

	/* Send initial capabilities */
	client_send_capabilities(client);
	client_send_ok(client, client->set->login_greeting);

	o_stream_uncork(client->output);

	client->banner_sent = TRUE;
}

static void managesieve_client_starttls(struct client *client)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *)client;

	managesieve_parser_destroy(&msieve_client->parser);
	msieve_client->parser = managesieve_parser_create(
		msieve_client->common.input, MAX_MANAGESIEVE_LINE);

	/* CRLF is lost from buffer when streams are reopened. */
	msieve_client->skip_line = FALSE;

	/* Cork the stream to send the capability data as a single tcp frame
	   Some naive clients break if we don't.
	 */
	o_stream_cork(client->output);

	client_send_capabilities(client);
	client_send_ok(client, "TLS negotiation successful.");

	o_stream_uncork(client->output);
}

static void
client_send_reply_raw(struct client *client, const char *prefix,
		      const char *resp_code, const char *text)
{
	T_BEGIN {
		string_t *line = t_str_new(256);

		str_append(line, prefix);

		if (resp_code != NULL) {
			str_append(line, " (");
			str_append(line, resp_code);
			str_append_c(line, ')');
		}

		if (text != NULL) {
			str_append_c(line, ' ');
			managesieve_quote_append_string(line, text, TRUE);
		}

		str_append(line, "\r\n");

		client_send_raw_data(client, str_data(line), str_len(line));
	} T_END;
}

void client_send_reply_code(struct client *client,
			    enum managesieve_cmd_reply reply,
			    const char *resp_code, const char *text)
{
	const char *prefix = "NO";

	switch (reply) {
	case MANAGESIEVE_CMD_REPLY_OK:
		prefix = "OK";
		break;
	case MANAGESIEVE_CMD_REPLY_NO:
		break;
	case MANAGESIEVE_CMD_REPLY_BYE:
		prefix = "BYE";
		break;
	}

	client_send_reply_raw(client, prefix, resp_code, text);
}

void client_send_reply(struct client *client, enum managesieve_cmd_reply reply,
		       const char *text)
{
	client_send_reply_code(client, reply, NULL, text);
}

static void
managesieve_client_notify_disconnect(struct client *client,
				     enum client_disconnect_reason reason,
				     const char *text)
{
	if (reason == CLIENT_DISCONNECT_SYSTEM_SHUTDOWN) {
		client_send_reply_code(client, MANAGESIEVE_CMD_REPLY_BYE,
				       "TRYLATER", text);
	} else {
		client_send_reply_code(client, MANAGESIEVE_CMD_REPLY_BYE,
				       NULL, text);
	}
}

static void managesieve_login_preinit(void)
{
	login_set_roots = managesieve_login_settings_set_roots;
}

static void managesieve_login_init(void)
{
}

static void managesieve_login_deinit(void)
{
	clients_destroy_all();
}

static struct client_vfuncs managesieve_client_vfuncs = {
	.alloc = managesieve_client_alloc,
	.create = managesieve_client_create,
	.destroy = managesieve_client_destroy,
	.notify_auth_ready = managesieve_client_notify_auth_ready,
	.notify_disconnect = managesieve_client_notify_disconnect,
	.notify_starttls = managesieve_client_notify_starttls,
	.starttls = managesieve_client_starttls,
	.input = managesieve_client_input,
	.auth_send_challenge = managesieve_client_auth_send_challenge,
	.auth_parse_response = managesieve_client_auth_parse_response,
	.auth_result = managesieve_client_auth_result,
	.proxy_reset = managesieve_proxy_reset,
	.proxy_parse_line = managesieve_proxy_parse_line,
	.proxy_failed = managesieve_proxy_failed,
	.proxy_get_state = managesieve_proxy_get_state,
	.send_raw_data = client_common_send_raw_data,
	.input_next_cmd = managesieve_client_input_next_cmd,
	.free = client_common_default_free,
};

static struct login_binary managesieve_login_binary = {
	.protocol = "sieve",
	.process_name = "managesieve-login",
	.default_port = 4190,

	.event_category = {
		.name = "managesieve",
	},

	.client_vfuncs = &managesieve_client_vfuncs,
	.preinit = managesieve_login_preinit,
	.init = managesieve_login_init,
	.deinit = managesieve_login_deinit,

	.anonymous_login_acceptable = FALSE,
};

int main(int argc, char *argv[])
{
	return login_binary_run(&managesieve_login_binary, argc, argv);
}
