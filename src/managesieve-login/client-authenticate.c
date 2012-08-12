/* Copyright (c) 2002-2012 Pigeonhole authors, see the included COPYING file
 */

#include "login-common.h"
#include "base64.h"
#include "buffer.h"
#include "hostpid.h"
#include "ioloop.h"
#include "istream.h"
#include "ostream.h"
#include "safe-memset.h"
#include "str.h"
#include "str-sanitize.h"
#include "time-util.h"
#include "auth-client.h"

#include "managesieve-parser.h"
#include "managesieve-quote.h"
#include "client.h"

#include "client-authenticate.h"
#include "managesieve-proxy.h"

#include <stdlib.h>

const char *client_authenticate_get_capabilities
(struct client *client)
{
	const struct auth_mech_desc *mech;
	unsigned int i, count;
	bool first = TRUE;
	string_t *str;

	str = t_str_new(128);
	mech = sasl_server_get_advertised_mechs(client, &count);

	for (i = 0; i < count; i++) {
		/* Filter ANONYMOUS mechanism, ManageSieve has no use-case for it */
		if ( (mech[i].flags & MECH_SEC_ANONYMOUS) == 0 ) {
			if ( !first )
				str_append_c(str, ' ');
			else
				first = FALSE;

			str_append(str, mech[i].name);
		}
	}

	return str_c(str);
}

bool managesieve_client_auth_handle_reply
(struct client *client, const struct client_auth_reply *reply)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *) client;
	const char *timestamp, *msg;

	if ( reply->host != NULL ) {
		string_t *resp_code;
		const char *reason;

		/* MANAGESIEVE referral

		   [nologin] referral host=.. [port=..] [destuser=..]
		   [reason=..]

		   NO [REFERRAL sieve://user;AUTH=mech@host:port/] "Can't login."
		   OK [...] "Logged in, but you should use this server instead."
		   .. [REFERRAL ..] Reason from auth server
		*/
		resp_code = t_str_new(128);
		str_printfa(resp_code, "REFERRAL sieve://%s;AUTH=%s@%s",
			    reply->destuser, client->auth_mech_name, reply->host);
		if ( reply->port != 4190 )
			str_printfa(resp_code, ":%u", reply->port);

		if ( reply->reason == NULL ) {
			if ( reply->nologin )
				reason = "Try this server instead.";
			else
				reason = "Logged in, but you should use "
					"this server instead.";
		} else {
			reason = reply->reason;
		}

		if ( !reply->nologin ) {
			client_send_okresp(client, str_c(resp_code), reason);
			client_destroy_success(client, "Login with referral");
			return TRUE;
 		}
		client_send_noresp(client, str_c(resp_code), reason);
	} else if (!reply->nologin) {
		/* normal login/failure */
		return FALSE;
	} else if (reply->reason != NULL) {
		client_send_line(client, CLIENT_CMD_REPLY_AUTH_FAIL_REASON,
			reply->reason);
	} else if (reply->temp) {
		timestamp = t_strflocaltime("%Y-%m-%d %H:%M:%S", ioloop_time);
		msg = t_strdup_printf(AUTH_TEMP_FAILED_MSG" [%s:%s]",
				      my_hostname, timestamp);
		client_send_line(client,
				 CLIENT_CMD_REPLY_AUTH_FAIL_TEMP, msg);
	} else if (reply->authz_failure) {
		client_send_line(client, CLIENT_CMD_REPLY_AUTHZ_FAILED,
				 "Authorization failed");
	} else {
		client_send_line(client, CLIENT_CMD_REPLY_AUTH_FAILED,
				 AUTH_FAILED_MSG);
	}

	i_assert(reply->nologin);

	msieve_client->auth_response_input = NULL;
	managesieve_parser_reset(msieve_client->parser);

	if ( !client->destroyed )
		client_auth_failed(client);
	return TRUE;
}

void managesieve_client_auth_send_challenge
(struct client *client, const char *data)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *) client;

	T_BEGIN {
		string_t *str = t_str_new(256);

		managesieve_quote_append_string(str, data, TRUE);
		str_append(str, "\r\n");

		client_send_raw_data(client, str_c(str), str_len(str));
	} T_END;

	msieve_client->auth_response_input = NULL;
	managesieve_parser_reset(msieve_client->parser);
}

static int managesieve_client_auth_read_response
(struct managesieve_client *msieve_client, bool initial, const char **error_r)
{
	struct client *client = &msieve_client->common;
	const struct managesieve_arg *args;
	const char *error;
	bool fatal;
	const unsigned char *data;
	size_t size;
	uoff_t resp_size;
	int ret;

	if ( i_stream_read(client->input) == -1 ) {
		/* disconnected */
		client_destroy(client, "Disconnected");
		return -1;
	}

	if ( msieve_client->auth_response_input == NULL ) {

		if ( msieve_client->skip_line ) {
			if ( i_stream_next_line(client->input) == NULL )
				return 0;

			msieve_client->skip_line = FALSE;
		}

		switch ( managesieve_parser_read_args(msieve_client->parser, 0,
			MANAGESIEVE_PARSE_FLAG_STRING_STREAM, &args) ) {
		case -1:
			error = managesieve_parser_get_error(msieve_client->parser, &fatal);
			if (fatal) {
				client_send_bye(client, error);
				client_destroy(client, t_strconcat
					("Disconnected: parse error during auth: ", error, NULL));
				*error_r = NULL;
			} else {
				*error_r = error;
			}
			msieve_client->skip_line = TRUE;
			return -1;

		case -2:
			/* not enough data */
			return 0;

		default:
			break;
		}

		if ( MANAGESIEVE_ARG_IS_EOL(&args[0]) ) {
			if (!initial) {
				*error_r = "Received empty AUTHENTICATE client response line.";
				msieve_client->skip_line = TRUE;
				return -1;
			}
			msieve_client->skip_line = TRUE;
			return 1;
		}

		if ( !managesieve_arg_get_string_stream
				(&args[0], &msieve_client->auth_response_input)
			|| !MANAGESIEVE_ARG_IS_EOL(&args[1]) ) {
			if ( !initial )
				*error_r = "Invalid AUTHENTICATE client response.";
			else
				*error_r = "Invalid AUTHENTICATE initial response.";
			msieve_client->skip_line = TRUE;
			return -1;
		}

		if ( i_stream_get_size
			(msieve_client->auth_response_input, FALSE, &resp_size) <= 0 )
			resp_size = 0;

		if (client->auth_response == NULL)
			client->auth_response = str_new(default_pool, I_MAX(resp_size+1, 256));
	}

	while ( (ret=i_stream_read_data
		(msieve_client->auth_response_input, &data, &size, 0) ) > 0 ) {

		if (str_len(client->auth_response) + size > LOGIN_MAX_AUTH_BUF_SIZE) {
			client_destroy(client, "Authentication response too large");
			*error_r = NULL;
			return -1;
		}

		str_append_n(client->auth_response, data, size);
		i_stream_skip(msieve_client->auth_response_input, size);
	}

	if ( ret == 0 ) return 0;

	if ( msieve_client->auth_response_input->stream_errno != 0 ) {
		if ( msieve_client->auth_response_input->stream_errno == EIO ) {
			error = managesieve_parser_get_error(msieve_client->parser, &fatal);
			if (error != NULL ) {
				if (fatal) {
					client_send_bye(client, error);
					client_destroy(client, t_strconcat
						("Disconnected: parse error during auth: ", error, NULL));
					*error_r = NULL;
				} else {
					msieve_client->skip_line = TRUE;
					*error_r = t_strconcat
						("Error in AUTHENTICATE response string: ", error, NULL);
				}
				return -1;
			}
		}

		client_destroy(client, "Disconnected");
		return -1;
	}

	if ( i_stream_next_line(client->input) == NULL )
		msieve_client->skip_line = TRUE;

	return 1;
}

int managesieve_client_auth_parse_response(struct client *client)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *) client;
	const char *error = NULL;
	int ret;

	if ( (ret=managesieve_client_auth_read_response(msieve_client, FALSE, &error))
		< 0 ) {
		if ( error != NULL )
			sasl_server_auth_failed(client, error);
		return -1;
	}

	if ( ret == 0 ) return 0;

	if ( strcmp(str_c(client->auth_response), "*") == 0 ) {
		sasl_server_auth_abort(client);
		return -1;
	}

	return 1;
}

int cmd_authenticate
(struct managesieve_client *msieve_client, const struct managesieve_arg *args)
{
	/* NOTE: This command's input is handled specially because the
	   SASL-IR can be large. */
	struct client *client = &msieve_client->common;
	const char *mech_name, *init_response;
	const char *error;
	int ret;

	if (!msieve_client->auth_mech_name_parsed) {
		i_assert(args != NULL);

		/* one mandatory argument: authentication mechanism name */
		if ( !managesieve_arg_get_string(&args[0], &mech_name) )
			return -1;

		if (*mech_name == '\0')
			return -1;

		/* Refuse the ANONYMOUS mechanism. */
		if ( strncasecmp(mech_name, "ANONYMOUS", 9) == 0 ) {
			client_send_no(client, "ANONYMOUS login is not allowed.");
			return 1;
		}

		i_free(client->auth_mech_name);
		client->auth_mech_name = i_strdup(mech_name);
		msieve_client->auth_mech_name_parsed = TRUE;

		msieve_client->auth_response_input = NULL;
		managesieve_parser_reset(msieve_client->parser);
	}

	msieve_client->skip_line = FALSE;
	if ( (ret=managesieve_client_auth_read_response(msieve_client, TRUE, &error))
		< 0 ) {
		if ( error != NULL )
			client_send_no(client, error);
		return 1;
	}

	if ( ret == 0 ) return 0;

	init_response = ( client->auth_response == NULL ? NULL :
		t_strdup(str_c(client->auth_response)) );
	msieve_client->auth_mech_name_parsed = FALSE;
	if ( (ret=client_auth_begin
		(client, t_strdup(client->auth_mech_name), init_response)) < 0 )
		return ret;

	msieve_client->cmd_finished = TRUE;
	return 0;
}

