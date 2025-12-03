/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "login-common.h"
#include "base64.h"
#include "buffer.h"
#include "ioloop.h"
#include "istream.h"
#include "ostream.h"
#include "str.h"
#include "str-sanitize.h"
#include "auth-client.h"

#include "managesieve-parser.h"
#include "managesieve-protocol.h"
#include "managesieve-quote.h"
#include "managesieve-url.h"
#include "client.h"

#include "client-authenticate.h"
#include "managesieve-proxy.h"

const char *client_authenticate_get_capabilities(struct client *client)
{
	const struct auth_mech_desc *mech;
	unsigned int i, count;
	string_t *str;

	str = t_str_new(128);
	mech = sasl_server_get_advertised_mechs(client, &count);

	for (i = 0; i < count; i++) {
		if (i > 0)
			str_append_c(str, ' ');
		str_append(str, mech[i].name);
	}

	return str_c(str);
}

void managesieve_client_auth_result(struct client *client,
				    enum client_auth_result result,
				    const struct client_auth_reply *reply,
				    const char *text)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *)client;
	struct managesieve_url url;
	string_t *referral;

	switch (result) {
	case CLIENT_AUTH_RESULT_SUCCESS:
		/* nothing to be done for IMAP */
		break;
	case CLIENT_AUTH_RESULT_REFERRAL_SUCCESS:
	case CLIENT_AUTH_RESULT_REFERRAL_NOLOGIN:
		/* MANAGESIEVE referral

		   [nologin] referral host .. [port=..] [destuser=..]
		   [reason=..]

		   NO [REFERRAL sieve://user;AUTH=mech@host:port/] "Can't login."
		   OK [...] "Logged in, but you should use this server instead."
		   .. [REFERRAL ..] Reason from auth server
		*/
		referral = t_str_new(128);

		i_zero(&url);
		url.user = reply->proxy.username;
		url.host.name = reply->proxy.host;
		url.host.ip = reply->proxy.host_ip;
		url.port = reply->proxy.port;
		str_append(referral, "REFERRAL ");
		str_append(referral, managesieve_url_create(&url));

		if (result == CLIENT_AUTH_RESULT_REFERRAL_SUCCESS)
			client_send_okresp(client, str_c(referral), text);
		else
			client_send_noresp(client, str_c(referral), text);
		break;
	case CLIENT_AUTH_RESULT_ABORTED:
	case CLIENT_AUTH_RESULT_AUTHFAILED_REASON:
	case CLIENT_AUTH_RESULT_AUTHZFAILED:
		client_send_no(client, text);
		break;
	case CLIENT_AUTH_RESULT_TEMPFAIL:
		client_send_noresp(client, "TRYLATER", text);
		break;
	case CLIENT_AUTH_RESULT_LIMIT_REACHED:
		client_send_noresp(client, "LIMIT/CONNECTIONS", text);
		break;
	case CLIENT_AUTH_RESULT_SSL_REQUIRED:
		client_send_noresp(client, "ENCRYPT-NEEDED", text);
		break;
	case CLIENT_AUTH_RESULT_AUTHFAILED:
	default:
		client_send_no(client,  text);
		break;
	}

	msieve_client->auth_response_input = NULL;
	managesieve_parser_reset(msieve_client->parser);
}

void managesieve_client_auth_send_challenge(
	struct client *client, const char *data)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *)client;

	T_BEGIN {
		string_t *str = t_str_new(256);

		managesieve_quote_append_string(str, data, TRUE);
		str_append(str, "\r\n");

		client_send_raw_data(client, str_c(str), str_len(str));
	} T_END;

	msieve_client->auth_response_input = NULL;
	managesieve_parser_reset(msieve_client->parser);
}

static int
managesieve_client_auth_read_response(struct managesieve_client *msieve_client,
				      bool initial, const char **error_r)
{
	struct client *client = &msieve_client->common;
	const struct managesieve_arg *args;
	const char *error;
	bool fatal;
	const unsigned char *data;
	size_t size;
	uoff_t resp_size;
	int ret;

	*error_r = NULL;

	if (i_stream_read(client->input) == -1) {
		/* Disconnected */
		client_destroy_iostream_error(client);
		return -1;
	}

	if (msieve_client->auth_response_input == NULL) {

		if (msieve_client->skip_line) {
			if (i_stream_next_line(client->input) == NULL)
				return 0;
			msieve_client->skip_line = FALSE;
		}

		switch (managesieve_parser_read_args(
			msieve_client->parser, 0,
			MANAGESIEVE_PARSE_FLAG_STRING_STREAM, &args)) {
		case -1:
			error = managesieve_parser_get_error(
				msieve_client->parser, &fatal);
			if (fatal) {
				client_send_bye(client, error);
				client_destroy(client, t_strconcat(
					"parse error during auth: ",
					error, NULL));
			} else {
				*error_r = error;
			}
			msieve_client->skip_line = TRUE;
			return -1;

		case -2:
			/* Not enough data */
			return 0;

		default:
			break;
		}

		if (MANAGESIEVE_ARG_IS_EOL(&args[0])) {
			if (!initial) {
				*error_r = "Received empty AUTHENTICATE client response line.";
				msieve_client->skip_line = TRUE;
				return -1;
			}
			msieve_client->skip_line = TRUE;
			return 1;
		}

		if (!managesieve_arg_get_string_stream(
			&args[0], &msieve_client->auth_response_input) ||
		    !MANAGESIEVE_ARG_IS_EOL(&args[1])) {
			if (!initial)
				*error_r = "Invalid AUTHENTICATE client response.";
			else
				*error_r = "Invalid AUTHENTICATE initial response.";
			msieve_client->skip_line = TRUE;
			return -1;
		}

		if (i_stream_get_size(msieve_client->auth_response_input,
				      FALSE, &resp_size) <= 0)
			resp_size = 0;

		if (client->auth_response == NULL) {
			client->auth_response =
				str_new(default_pool, I_MAX(resp_size+1, 256));
		}
	}

	while ((ret = i_stream_read_more(msieve_client->auth_response_input,
					 &data, &size)) > 0) {
		if ((str_len(client->auth_response) + size) >
		    LOGIN_MAX_AUTH_BUF_SIZE) {
			client_destroy(client,
				       "Authentication response too large");
			return -1;
		}

		str_append_data(client->auth_response, data, size);
		i_stream_skip(msieve_client->auth_response_input, size);
	}

	if (ret == 0)
		return 0;

	if (msieve_client->auth_response_input->stream_errno != 0) {
		if (!client->input->eof &&
		    msieve_client->auth_response_input->stream_errno == EINVAL) {
			msieve_client->skip_line = TRUE;
			*error_r = t_strconcat(
				"Error in AUTHENTICATE response string: ",
				i_stream_get_error(
					msieve_client->auth_response_input),
				NULL);
			return -1;
		}

		client_destroy_iostream_error(client);
		return -1;
	}

	if (i_stream_next_line(client->input) == NULL)
		return 0;

	return 1;
}

bool managesieve_client_auth_parse_response(struct client *client)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *)client;
	const char *error = NULL;
	int ret;

	ret = managesieve_client_auth_read_response(msieve_client, FALSE,
						    &error);
	if (ret < 0) {
		if (error != NULL)
			client_auth_fail(client, error);
		return FALSE;
	}
	if (ret == 0)
		return FALSE;
	return TRUE;
}

int cmd_authenticate(struct managesieve_client *msieve_client,
		     const struct managesieve_arg *args)
{
	/* NOTE: This command's input is handled specially because the
	   SASL-IR can be large. */
	struct client *client = &msieve_client->common;
	const char *mech_name, *init_response;
	const char *error;
	int ret;

	if (!msieve_client->auth_mech_name_parsed) {
		i_assert(args != NULL);

		/* One mandatory argument: authentication mechanism name */
		if (!managesieve_arg_get_string(&args[0], &mech_name))
			return -1;
		if (*mech_name == '\0')
			return -1;

		i_free(client->auth_mech_name);
		client->auth_mech_name = i_strdup(mech_name);
		msieve_client->auth_mech_name_parsed = TRUE;

		msieve_client->auth_response_input = NULL;
		managesieve_parser_reset(msieve_client->parser);
	}

	msieve_client->skip_line = FALSE;
	ret = managesieve_client_auth_read_response(msieve_client, TRUE,
						    &error);
	if (ret < 0) {
		msieve_client->auth_mech_name_parsed = FALSE;
		if (error != NULL)
			client_send_no(client, error);
		return 1;
	}
	if (ret == 0)
		return 0;

	init_response = (client->auth_response == NULL ?
			 NULL : t_strdup(str_c(client->auth_response)));
	msieve_client->auth_mech_name_parsed = FALSE;
	ret = client_auth_begin(client, t_strdup(client->auth_mech_name),
				init_response);
	if (ret < 0)
		return ret;

	msieve_client->cmd_finished = TRUE;
	return 0;
}
