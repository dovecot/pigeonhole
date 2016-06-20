/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include <string.h>
#include "login-common.h"
#include "ioloop.h"
#include "istream.h"
#include "ostream.h"
#include "str.h"
#include "str-sanitize.h"
#include "safe-memset.h"
#include "buffer.h"
#include "base64.h"
#include "dsasl-client.h"

#include "client.h"
#include "client-authenticate.h"

#include "managesieve-quote.h"
#include "managesieve-proxy.h"
#include "managesieve-parser.h"

enum {
	MSIEVE_PROXY_STATE_NONE,
	MSIEVE_PROXY_STATE_TLS_START,
	MSIEVE_PROXY_STATE_TLS_READY,
	MSIEVE_PROXY_STATE_XCLIENT,
	MSIEVE_PROXY_STATE_AUTH,
};

typedef enum {
	MANAGESIEVE_RESPONSE_NONE,
	MANAGESIEVE_RESPONSE_OK,
	MANAGESIEVE_RESPONSE_NO,
	MANAGESIEVE_RESPONSE_BYE
} managesieve_response_t;

static void proxy_free_password(struct client *client)
{
	if (client->proxy_password == NULL)
		return;

	safe_memset(client->proxy_password, 0, strlen(client->proxy_password));
	i_free_and_null(client->proxy_password);
}

static void proxy_write_xclient
(struct managesieve_client *client, string_t *str)
{
	str_printfa(str,
		"XCLIENT ADDR=%s PORT=%u SESSION=%s TTL=%u\r\n",
		net_ip2addr(&client->common.ip),
		client->common.remote_port,
		client_get_session_id(&client->common),
		client->common.proxy_ttl - 1);
}

static void proxy_write_auth_data
(const unsigned char *data, unsigned int data_len,
	string_t *str)
{
	if (data_len == 0)
		str_append(str, "\"\"");
	else {
		string_t *data_str = t_str_new(128);
		base64_encode(data, data_len, data_str);
		managesieve_quote_append_string(str, str_c(data_str), FALSE);
	}
}

static int proxy_write_auth
(struct managesieve_client *client, string_t *str)
{
	struct dsasl_client_settings sasl_set;
	const unsigned char *output;
	unsigned int len;
	const char *mech_name, *error;

	i_assert(client->common.proxy_ttl > 1);

	if ( !client->proxy_sasl ) {
		/* Prevent sending credentials to a server that has login disabled;
		   i.e., due to the lack of TLS */
		client_log_err(&client->common, "proxy: "
			"Server has disabled authentication (TLS required?)");
		return -1;
	}

	if (client->common.proxy_mech == NULL)
		client->common.proxy_mech = &dsasl_client_mech_plain;

	i_assert(client->common.proxy_sasl_client == NULL);
	memset(&sasl_set, 0, sizeof(sasl_set));
	sasl_set.authid = client->common.proxy_master_user != NULL ?
		client->common.proxy_master_user : client->common.proxy_user;
	sasl_set.authzid = client->common.proxy_user;
	sasl_set.password = client->common.proxy_password;
	client->common.proxy_sasl_client =
		dsasl_client_new(client->common.proxy_mech, &sasl_set);
	mech_name = dsasl_client_mech_get_name(client->common.proxy_mech);

	str_append(str, "AUTHENTICATE ");
	managesieve_quote_append_string(str, mech_name, FALSE);
	if (dsasl_client_output(client->common.proxy_sasl_client,
				&output, &len, &error) < 0) {
		client_log_err(&client->common, t_strdup_printf(
			"proxy: SASL mechanism %s init failed: %s",
			mech_name, error));
		return -1;
	}
	if (len > 0) {
		str_append_c(str, ' ');
		proxy_write_auth_data(output, len, str);
	}
	str_append(str, "\r\n");
	proxy_free_password(&client->common);
	return 0;
}

static int proxy_input_auth_challenge
(struct managesieve_client *client, const char *line,
	const char **challenge_r)
{
	struct istream *input;
	struct managesieve_parser *parser;
 	const struct managesieve_arg *args;
	const char *challenge;
	bool fatal = FALSE;
	int ret;

	i_assert(client->common.proxy_sasl_client != NULL);
	*challenge_r = NULL;

	/* Build an input stream for the managesieve parser
	 *  FIXME: Ugly, see proxy_input_capability().
	 */
	line = t_strconcat(line, "\r\n", NULL);
	input = i_stream_create_from_data(line, strlen(line));
	parser = managesieve_parser_create(input, MAX_MANAGESIEVE_LINE);
	managesieve_parser_reset(parser);

	(void)i_stream_read(input);
	ret = managesieve_parser_read_args(parser, 1, 0, &args);

	if ( ret >= 0 ) {
		if ( ret > 0 && managesieve_arg_get_string(&args[0], &challenge) ) {
			*challenge_r = t_strdup(challenge);
		} else {
			client_log_err(&client->common, t_strdup_printf("proxy: "
				"Server sent invalid SASL challenge line: %s",
				str_sanitize(line,160)));
			fatal = TRUE;
		}

	} else if ( ret == -2 ) {
		/* Parser needs more data (not possible on mem stream) */
		i_unreached();

	} else {
		const char *error_str = managesieve_parser_get_error(parser, &fatal);
		error_str = (error_str != NULL ? error_str : "unknown (bug)" );

		/* Do not accept faulty server */
		client_log_err(&client->common, t_strdup_printf("proxy: "
			"Protocol parse error(%d) int SASL challenge line: %s (line=`%s')",
			ret, error_str, line));
		fatal = TRUE;
	}


	/* Cleanup parser */
  managesieve_parser_destroy(&parser);
	i_stream_destroy(&input);

	/* Time to exit if greeting was not accepted */
	if ( fatal ) return -1;
	return 0;
}

static int proxy_write_auth_response
(struct managesieve_client *client,
	const char *challenge, string_t *str)
{
	const unsigned char *data;
	unsigned int data_len;
	const char *error;
	int ret;

	if (base64_decode(challenge, strlen(challenge), NULL, str) < 0) {
		client_log_err(&client->common,
			"proxy: Server sent invalid base64 data in AUTHENTICATE response");
		return -1;
	}
	ret = dsasl_client_input(client->common.proxy_sasl_client,
				 str_data(str), str_len(str), &error);
	if (ret == 0) {
		ret = dsasl_client_output(client->common.proxy_sasl_client,
					  &data, &data_len, &error);
	}
	if (ret < 0) {
		client_log_err(&client->common, t_strdup_printf(
			"proxy: Server sent invalid authentication data: %s",
			error));
		return -1;
	}
	i_assert(ret == 0);

	str_truncate(str, 0);
	proxy_write_auth_data(data, data_len, str);
	str_append(str, "\r\n");
	return 0;
}

static managesieve_response_t proxy_read_response
(const struct managesieve_arg *args)
{
	const char *response;

	if ( managesieve_arg_get_atom(&args[0], &response) ) {
		if ( strcasecmp(response, "OK") == 0 ) {
			/* Received OK response; greeting is finished */
			return MANAGESIEVE_RESPONSE_OK;

		} else if ( strcasecmp(response, "NO") == 0 ) {
			/* Received OK response; greeting is finished */
			return MANAGESIEVE_RESPONSE_NO;

		} else if ( strcasecmp(response, "BYE") == 0 ) {
			/* Received OK response; greeting is finished */
			return MANAGESIEVE_RESPONSE_BYE;
		}
	}
	return MANAGESIEVE_RESPONSE_NONE;
}

static int proxy_input_capability
(struct managesieve_client *client, const char *line,
	managesieve_response_t *resp_r)
{
	struct istream *input;
	struct managesieve_parser *parser;
 	const struct managesieve_arg *args;
	const char *capability;
	int ret;
	bool fatal = FALSE;

	*resp_r = MANAGESIEVE_RESPONSE_NONE;

	/* Build an input stream for the managesieve parser
	 *  FIXME: It would be nice if the line-wise parsing could be
	 *    substituded by something similar to the command line interpreter.
	 *    However, the current login_proxy structure does not make streams
	 *    known until inside proxy_input handler.
	 */
	line = t_strconcat(line, "\r\n", NULL);
	input = i_stream_create_from_data(line, strlen(line));
	parser = managesieve_parser_create(input, MAX_MANAGESIEVE_LINE);
	managesieve_parser_reset(parser);

	/* Parse input
	 *  FIXME: Theoretically the OK response could include a
	 *   response code which could be rejected by the parser.
	 */
	(void)i_stream_read(input);
	ret = managesieve_parser_read_args(parser, 2, 0, &args);

	if ( ret == 0 ) {
		client_log_err(&client->common, t_strdup_printf("proxy: "
			"Remote returned with invalid capability/greeting line: %s",
			str_sanitize(line,160)));
		fatal = TRUE;
	} else if ( ret > 0 ) {
		if ( args[0].type == MANAGESIEVE_ARG_ATOM ) {
			*resp_r = proxy_read_response(args);

			if ( *resp_r == MANAGESIEVE_RESPONSE_NONE ) {
				client_log_err(&client->common, t_strdup_printf("proxy: "
					"Remote sent invalid response: %s",
					str_sanitize(line,160)));

				fatal = TRUE;
			}
		} else if ( managesieve_arg_get_string(&args[0], &capability) ) {
			if ( strcasecmp(capability, "SASL") == 0 ) {
				const char *sasl_mechs;

				/* Check whether the server supports the SASL mechanism
				 * we are going to use (currently only PLAIN supported).
				 */
				if ( ret == 2 && managesieve_arg_get_string(&args[1], &sasl_mechs) ) {
					const char *const *mechs = t_strsplit(sasl_mechs, " ");

					if ( *mechs != NULL ) {
						/* At least one SASL mechanism is supported */
						client->proxy_sasl = TRUE;
					}

				} else {
					client_log_err(&client->common, "proxy: "
		         		"Server returned erroneous SASL capability");
					fatal = TRUE;
				}

			} else if ( strcasecmp(capability, "STARTTLS") == 0 ) {
				client->proxy_starttls = TRUE;
			} else if ( strcasecmp(capability, "XCLIENT") == 0 ) {
				client->proxy_xclient = TRUE;
			}

		} else {
			/* Do not accept faulty server */
			client_log_err(&client->common, t_strdup_printf("proxy: "
				"Remote returned with invalid capability/greeting line: %s",
				str_sanitize(line,160)));
			fatal = TRUE;
		}

	} else if ( ret == -2 ) {
		/* Parser needs more data (not possible on mem stream) */
		i_unreached();

	} else {
		const char *error_str = managesieve_parser_get_error(parser, &fatal);
		error_str = (error_str != NULL ? error_str : "unknown (bug)" );

		/* Do not accept faulty server */
		client_log_err(&client->common, t_strdup_printf("proxy: "
			"Protocol parse error(%d) in capability/greeting line: %s (line=`%s')",
			ret, error_str, line));
		fatal = TRUE;
	}

	/* Cleanup parser */
	managesieve_parser_destroy(&parser);
	i_stream_destroy(&input);

	/* Time to exit if greeting was not accepted */
	if ( fatal ) return -1;

	/* Wait until greeting is received completely */
	if ( *resp_r == MANAGESIEVE_RESPONSE_NONE ) return 1;

	return 0;
}

int managesieve_proxy_parse_line(struct client *client, const char *line)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *) client;
	struct ostream *output;
	enum login_proxy_ssl_flags ssl_flags;
	managesieve_response_t response = MANAGESIEVE_RESPONSE_NONE;
	string_t *command;
	int ret = 0;

	i_assert(!client->destroyed);

	output = login_proxy_get_ostream(client->login_proxy);
	switch ( msieve_client->proxy_state ) {
	case MSIEVE_PROXY_STATE_NONE:
		if ( (ret=proxy_input_capability
			(msieve_client, line, &response)) < 0 ) {
			client_proxy_failed(client, TRUE);
			return -1;
		}

		if ( ret == 0 ) {
			if ( response != MANAGESIEVE_RESPONSE_OK ) {
				client_log_err(client, "proxy: "
					"Remote sent unexpected NO/BYE instead of capability response");
				client_proxy_failed(client, TRUE);
				return -1;
			}

			command = t_str_new(128);

			ssl_flags = login_proxy_get_ssl_flags(client->login_proxy);
			if ((ssl_flags & PROXY_SSL_FLAG_STARTTLS) != 0) {
				if ( !msieve_client->proxy_starttls ) {
					client_log_err(client, "proxy: Remote doesn't support STARTTLS");
						client_proxy_failed(client, TRUE);
					return -1;
				}

				str_append(command, "STARTTLS\r\n");
				msieve_client->proxy_state = MSIEVE_PROXY_STATE_TLS_START;

			} else if (msieve_client->proxy_xclient) {
				proxy_write_xclient(msieve_client, command);
				msieve_client->proxy_state = MSIEVE_PROXY_STATE_XCLIENT;

			} else {
				if ( proxy_write_auth(msieve_client, command) < 0 ) {
					client_proxy_failed(client, TRUE);
					return -1;
				}
				msieve_client->proxy_state = MSIEVE_PROXY_STATE_AUTH;
			}

			(void)o_stream_send(output, str_data(command), str_len(command));
		}
		return 0;

	case MSIEVE_PROXY_STATE_TLS_START:
		if ( strncasecmp(line, "OK", 2) == 0 &&
			( strlen(line) == 2 || line[2] == ' ' ) ) {

			/* STARTTLS successful, begin TLS negotiation. */
			if ( login_proxy_starttls(client->login_proxy) < 0 ) {
				client_proxy_failed(client, TRUE);
				return -1;
			}

			msieve_client->proxy_sasl = FALSE;
			msieve_client->proxy_xclient = FALSE;
			msieve_client->proxy_state = MSIEVE_PROXY_STATE_TLS_READY;
			return 1;
		}

		client_log_err(client, "proxy: Remote refused STARTTLS command");
		client_proxy_failed(client, TRUE);
		return -1;

	case MSIEVE_PROXY_STATE_TLS_READY:
		if ( (ret=proxy_input_capability(msieve_client, line, &response)) < 0 ) {
			client_proxy_failed(client, TRUE);
			return -1;
		}

		if ( ret == 0 ) {
			if ( response != MANAGESIEVE_RESPONSE_OK ) {
				/* STARTTLS failed */
				client_log_err(client,
					t_strdup_printf("proxy: Remote STARTTLS failed: %s",
						str_sanitize(line, 160)));
				client_proxy_failed(client, TRUE);
				return -1;
			}

			command = t_str_new(128);
			if ( msieve_client->proxy_xclient ) {
				proxy_write_xclient(msieve_client, command);
				msieve_client->proxy_state = MSIEVE_PROXY_STATE_XCLIENT;

			} else {
				if ( proxy_write_auth(msieve_client, command) < 0 ) {
					client_proxy_failed(client, TRUE);
					return -1;
				}
				msieve_client->proxy_state = MSIEVE_PROXY_STATE_AUTH;
			}
			(void)o_stream_send(output, str_data(command), str_len(command));
		}
		return 0;

	case MSIEVE_PROXY_STATE_XCLIENT:
		if ( strncasecmp(line, "OK", 2) == 0 &&
			( strlen(line) == 2 || line[2] == ' ' ) ) {

			command = t_str_new(128);
			if ( proxy_write_auth(msieve_client, command) < 0 ) {
				client_proxy_failed(client, TRUE);
				return -1;
			}
			(void)o_stream_send(output, str_data(command), str_len(command));
			msieve_client->proxy_state = MSIEVE_PROXY_STATE_AUTH;
			return 0;
		}

		client_log_err(client, t_strdup_printf(
			"proxy: Remote XCLIENT failed: %s",
			str_sanitize(line, 160)));
		client_proxy_failed(client, TRUE);
		return -1;

	case MSIEVE_PROXY_STATE_AUTH:
		/* Challenge? */
		if ( *line == '"' ) {
			const char *challenge;

			if ( proxy_input_auth_challenge
				(msieve_client, line, &challenge) < 0 ) {
				client_proxy_failed(client, TRUE);
				return -1;
			}
			command = t_str_new(128);
			if ( proxy_write_auth_response
				(msieve_client, challenge, command) < 0 ) {
				client_proxy_failed(client, TRUE);
				return -1;
			}
			(void)o_stream_send(output, str_data(command), str_len(command));
			return 0;
		}

		/* Check login status */
		if ( strncasecmp(line, "OK", 2) == 0 &&
			(strlen(line) == 2 || line[2] == ' ') ) {
			string_t *str = t_str_new(128);

			/* Login successful */

			/* FIXME: some SASL mechanisms cause a capability response to be sent */

			/* Send this line to client. */
			str_append(str, line );
			str_append(str, "\r\n");
			(void)o_stream_send(client->output, str_data(str), str_len(str));

			(void)client_skip_line(msieve_client);
			client_proxy_finish_destroy_client(client);
			return 1;
		}

		/* Authentication failed */
		if ( client->set->auth_verbose ) {
			const char *log_line = line;

			if (strncasecmp(log_line, "NO ", 3) == 0)
				log_line += 3;
			client_proxy_log_failure(client, log_line);
		}

		/* FIXME: properly parse and handle response codes */

		/* Login failed. Send our own failure reply so client can't
		 * figure out if user exists or not just by looking at the
		 * reply string.
		 */
		client_send_no(client, AUTH_FAILED_MSG);

		client->proxy_auth_failed = TRUE;
		client_proxy_failed(client, FALSE);
		return -1;

	default:
		/* Not supposed to happen */
		break;
	}

	i_unreached();
	return -1;
}

void managesieve_proxy_reset(struct client *client)
{
	struct managesieve_client *msieve_client =
		(struct managesieve_client *) client;

	msieve_client->proxy_starttls = FALSE;
	msieve_client->proxy_sasl = FALSE;
	msieve_client->proxy_xclient = FALSE;
	msieve_client->proxy_state = MSIEVE_PROXY_STATE_NONE;
}

void managesieve_proxy_error(struct client *client, const char *text)
{
	client_send_reply_code(client, MANAGESIEVE_CMD_REPLY_NO, "TRYLATER", text);
}
