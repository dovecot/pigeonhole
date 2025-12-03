/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "login-common.h"
#include "buffer.h"
#include "connection.h"
#include "ioloop.h"
#include "istream.h"
#include "ostream.h"
#include "str.h"
#include "strescape.h"
#include "base64.h"
#include "settings.h"
#include "master-service.h"
#include "auth-client.h"

#include "managesieve-parser.h"
#include "managesieve-protocol.h"
#include "managesieve-quote.h"

#include "client.h"
#include "client-authenticate.h"

#include "managesieve-login-settings.h"
#include "managesieve-proxy.h"

/* Disconnect client when it sends too many bad commands */
#define CLIENT_MAX_BAD_COMMANDS 3

struct managesieve_command {
	const char *name;
	int (*func)(struct managesieve_client *msieve_client,
		    const struct managesieve_arg *args);
	int preparsed_args;
};

/* Skip incoming data until newline is found,
   returns TRUE if newline was found. */
bool client_skip_line(struct managesieve_client *msieve_client)
{
	struct client *client = &msieve_client->common;
	const unsigned char *data;
	size_t i, data_size;

	data = i_stream_get_data(client->input, &data_size);

	for (i = 0; i < data_size; i++) {
		if (data[i] == '\n') {
			i_stream_skip(client->input, i+1);
			return TRUE;
		}
	}

	return FALSE;
}

static void client_send_capabilities(struct client *client)
{
	struct managesieve_client *msieve_client =
		container_of(client, struct managesieve_client, common);
	const ARRAY_TYPE(const_string) *sieve_cap_list =
		&msieve_client->set->managesieve_sieve_capability;
	const ARRAY_TYPE(const_string) *notify_cap_list =
		&msieve_client->set->managesieve_notify_capability;
	const ARRAY_TYPE(const_string) *extlists_cap_list =
		&msieve_client->set->managesieve_extlists_capability;
	const char *sieve_cap, *notify_cap, *extlists_cap, *sasl_cap;

	T_BEGIN {
		sieve_cap = t_strarray_join(
			settings_boollist_get(sieve_cap_list), " ");
		notify_cap = t_strarray_join(
			settings_boollist_get(notify_cap_list), " ");
		extlists_cap = t_strarray_join(
			settings_boollist_get(extlists_cap_list), " ");
		sasl_cap = client_authenticate_get_capabilities(client);

		/* Default capabilities */
		client_send_raw(client, t_strconcat(
			"\"IMPLEMENTATION\" \"",
			msieve_client->set->managesieve_implementation_string,
			"\"\r\n", NULL));
		client_send_raw(client, t_strconcat(
				"\"SIEVE\" \"", sieve_cap, "\"\r\n", NULL));
		if (notify_cap[0] != '\0') {
			client_send_raw(client, t_strconcat(
				"\"NOTIFY\" \"", notify_cap, "\"\r\n", NULL));
		}
		if (extlists_cap[0] != '\0') {
			client_send_raw(client, t_strconcat(
				"\"EXTLISTS\" \"", extlists_cap,
				"\"\r\n", NULL));
		}
		client_send_raw(client, t_strconcat("\"SASL\" \"", sasl_cap,
						    "\"\r\n", NULL));

		/* STARTTLS */
		if (login_ssl_initialized && !client->connection_tls_secured)
			client_send_raw(client, "\"STARTTLS\"\r\n");

		/* Protocol version */
		client_send_raw(client, "\"VERSION\" \"1.0\"\r\n");

		/* XCLIENT */
		if (client->connection_trusted)
			client_send_raw(client, "\"XCLIENT\"\r\n");
	} T_END;
}

static int
cmd_capability(struct managesieve_client *msieve_client,
	       const struct managesieve_arg *args ATTR_UNUSED)
{
	struct client *client = &msieve_client->common;

	o_stream_cork(client->output);

	client_send_capabilities(client);
	client_send_ok(client, "Capability completed.");

	o_stream_uncork(client->output);

	return 1;
}

static int
cmd_starttls(struct managesieve_client *msieve_client,
	     const struct managesieve_arg *args ATTR_UNUSED)
{
	struct client *client = &msieve_client->common;

	client_cmd_starttls(client);
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
cmd_noop(struct managesieve_client *msieve_client,
	 const struct managesieve_arg *args)
{
	struct client *client = &msieve_client->common;
	const char *text;
	string_t *resp_code;

	if (MANAGESIEVE_ARG_IS_EOL(&args[0])) {
		client_send_ok(client, "NOOP Completed");
		return 1;
	}
	if (!MANAGESIEVE_ARG_IS_EOL(&args[1]))
		return -1;
	if (!managesieve_arg_get_string(&args[0], &text)) {
		client_send_no(client, "Invalid echo tag.");
		return 1;
	}

	resp_code = t_str_new(256);
	str_append(resp_code, "TAG ");
	managesieve_quote_append_string(resp_code, text, FALSE);

	client_send_okresp(client, str_c(resp_code), "Done");
	return 1;
}

static int
cmd_logout(struct managesieve_client *msieve_client,
	   const struct managesieve_arg *args ATTR_UNUSED)
{
	struct client *client = &msieve_client->common;

	client_send_ok(client, "Logout completed.");
	client_destroy(client, CLIENT_UNAUTHENTICATED_LOGOUT_MSG);
	return 1;
}

static int
cmd_xclient(struct managesieve_client *msieve_client,
	    const struct managesieve_arg *args)
{
	struct client *client = &msieve_client->common;
	const char *value, *arg;
	bool args_ok = TRUE;

	if (!client->connection_trusted) {
		client_send_no(client, "You are not from trusted IP");
		return 1;
	}
	while (!MANAGESIEVE_ARG_IS_EOL(&args[0]) &&
		managesieve_arg_get_atom(&args[0], &arg)) {
		if (str_begins_icase(arg, "ADDR=", &value)) {
			if (net_addr2ip(value, &client->ip) < 0)
				args_ok = FALSE;
		} else if (str_begins_icase(arg, "FORWARD=", &value)) {
			if (!client_forward_decode_base64(client, value))
				args_ok = FALSE;
		} else if (str_begins_icase(arg, "PORT=", &value)) {
			if (net_str2port(value, &client->remote_port) < 0)
				args_ok = FALSE;
		} else if (str_begins_icase(arg, "SESSION=", &value)) {
			if (strlen(value) <= LOGIN_MAX_SESSION_ID_LEN) {
				client->session_id =
					p_strdup(client->pool, value);
			}
		} else if (str_begins_icase(arg, "TTL=", &value)) {
			if (str_to_uint(value, &client->proxy_ttl) < 0)
				args_ok = FALSE;
		} else if (str_begins_icase(arg, "CLIENT-TRANSPORT=", &value)) {
			client->end_client_tls_secured_set = TRUE;
			client->end_client_tls_secured =
				str_begins_with(value, CLIENT_TRANSPORT_TLS);
		} else if (str_begins_icase(arg, "DESTNAME=", &value)) {
			if (connection_is_valid_dns_name(value)) {
				client->local_name =
					p_strdup(client->pool, value);
			} else {
				args_ok = FALSE;
			}
		}
		args++;
	}
	if (!args_ok || !MANAGESIEVE_ARG_IS_EOL(&args[0]))
		return -1;

	client_send_ok(client, "Updated");
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

static bool client_handle_input(struct managesieve_client *msieve_client)
{
	struct client *client = &msieve_client->common;

	i_assert(!client->authenticating);

	if (msieve_client->cmd_finished) {
		/* Clear the previous command from memory */
		msieve_client->cmd_name = NULL;
		msieve_client->cmd_parsed_args = FALSE;
		msieve_client->cmd = NULL;
		managesieve_parser_reset(msieve_client->parser);

		/* Remove \r\n */
		if (msieve_client->skip_line) {
			if (!client_skip_line(msieve_client))
				return FALSE;
			msieve_client->skip_line = FALSE;
		}

		msieve_client->cmd_finished = FALSE;
	}

	if (msieve_client->cmd == NULL) {
		struct managesieve_command *cmd;
		const char *cmd_name;

		msieve_client->cmd_name =
			managesieve_parser_read_word(msieve_client->parser);
		if (msieve_client->cmd_name == NULL)
			return FALSE; /* Need more data */

		cmd_name = t_str_ucase(msieve_client->cmd_name);
		cmd = commands;
		while (cmd->name != NULL) {
			if (strcmp(cmd->name, cmd_name) == 0)
				break;
			cmd++;
		}

		if (cmd->name != NULL)
			msieve_client->cmd = cmd;
		else
			msieve_client->skip_line = TRUE;
	}
	return client->v.input_next_cmd(client);
}

static bool managesieve_client_input_next_cmd(struct client *client)
{
	struct managesieve_client *msieve_client =
		container_of(client, struct managesieve_client, common);
	const struct managesieve_arg *args = NULL;
	const char *msg;
	int ret = 1;
	bool fatal;

	if (msieve_client->cmd == NULL) {
		/* Unknown command */
		ret = -1;
	} else if (!msieve_client->cmd_parsed_args) {
		unsigned int arg_count =
			(msieve_client->cmd->preparsed_args > 0 ?
			 msieve_client->cmd->preparsed_args : 0);

		switch (managesieve_parser_read_args(msieve_client->parser,
						     arg_count, 0, &args)) {
		case -1:
			/* Error */
			msg = managesieve_parser_get_error(
				msieve_client->parser, &fatal);
			if (fatal) {
				client_send_bye(client, msg);
				client_destroy(client, msg);
				return FALSE;
			}
			client_send_no(client, msg);
			msieve_client->cmd_finished = TRUE;
			msieve_client->skip_line = TRUE;
			return TRUE;
		case -2:
			/* Not enough data */
			return FALSE;
		}
		i_assert(args != NULL);

		if (arg_count == 0) {
			/* We read the entire line - skip over the CRLF */
			if (!client_skip_line(msieve_client))
				i_unreached();
		} else {
			/* Get rid of it later */
			msieve_client->skip_line = TRUE;
		}

		msieve_client->cmd_parsed_args = TRUE;

		if (msieve_client->cmd->preparsed_args == -1) {
			/* Check absence of arguments */
			if (args[0].type != MANAGESIEVE_ARG_EOL)
				ret = -1;
		}
	}
	if (ret > 0) {
		i_assert(args != NULL);
		i_assert(msieve_client->cmd != NULL);
		ret = msieve_client->cmd->func(msieve_client, args);
	}

	if (ret != 0)
		msieve_client->cmd_finished = TRUE;
	if (ret < 0) {
		if (++client->bad_counter >= CLIENT_MAX_BAD_COMMANDS) {
			client_send_bye(client,
				"Too many invalid MANAGESIEVE commands.");
			client_destroy(client, "Too many invalid commands.");
			return FALSE;
		}
		client_send_no(client,
			"Error in MANAGESIEVE command received by server.");
	}

	return ret != 0 && !client->destroyed;
}

static void managesieve_client_input(struct client *client)
{
	struct managesieve_client *msieve_client =
		container_of(client, struct managesieve_client, common);

	if (!client_read(client))
		return;

	client_ref(client);
	o_stream_cork(msieve_client->common.output);
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
			if (!client_handle_input(msieve_client))
				break;
		}
	}
	o_stream_uncork(client->output);
	client_unref(&client);
}

static struct client *managesieve_client_alloc(pool_t pool)
{
	struct managesieve_client *msieve_client;

	msieve_client = p_new(pool, struct managesieve_client, 1);
	return &msieve_client->common;
}

static int managesieve_client_create(struct client *client)
{
	struct managesieve_client *msieve_client =
		container_of(client, struct managesieve_client, common);
	const char *error;

	if (settings_get(client->event, &managesieve_login_setting_parser_info, 0,
			 &msieve_client->set, &error) < 0) {
		e_error(client->event, "%s", error);
		return -1;
	}
	msieve_client->parser = managesieve_parser_create(client->input,
							  MAX_MANAGESIEVE_LINE);
	return 0;
}

static void managesieve_client_destroy(struct client *client)
{
	struct managesieve_client *msieve_client =
		container_of(client, struct managesieve_client, common);

	managesieve_parser_destroy(&msieve_client->parser);
	settings_free(msieve_client->set);
}

static int
managesieve_client_reload_config(struct client *client, const char **error_r)
{
	struct managesieve_client *msieve_client =
		container_of(client, struct managesieve_client, common);

	settings_free(msieve_client->set);
	return settings_get(client->event,
			    &managesieve_login_setting_parser_info, 0,
			    &msieve_client->set, error_r);
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
	i_assert(client->io == NULL);
	client->io = io_add_istream(client->input, client_input, client);
}

static void managesieve_client_starttls(struct client *client)
{
	struct managesieve_client *msieve_client =
		container_of(client, struct managesieve_client, common);

	managesieve_parser_destroy(&msieve_client->parser);
	msieve_client->parser = managesieve_parser_create(client->input,
							  MAX_MANAGESIEVE_LINE);

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
	.reload_config = managesieve_client_reload_config,
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
	.service_name = "managesieve",
	.process_name = "managesieve-login",
	.default_port = MANAGESIEVE_DEFAULT_PORT,

	.event_category = {
		.name = "managesieve",
	},

	.client_vfuncs = &managesieve_client_vfuncs,
	.preinit = managesieve_login_preinit,
	.init = managesieve_login_init,
	.deinit = managesieve_login_deinit,

	.anonymous_login_acceptable = FALSE,

	.application_protocols = (const char *const[]) {
		"managesieve", NULL
	},
};

int main(int argc, char *argv[])
{
	return login_binary_run(&managesieve_login_binary, argc, argv);
}
