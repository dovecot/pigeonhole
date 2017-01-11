/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "managesieve-quote.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"


bool cmd_noop(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	const struct managesieve_arg *args;
	const char *text;
	string_t *resp_code;

	/* [<echo string>] */
	if ( !client_read_args(cmd, 0, 0, FALSE, &args) )
		return FALSE;

	if ( MANAGESIEVE_ARG_IS_EOL(&args[0]) ) {
		client_send_ok(client, "NOOP Completed");
		return TRUE;
	}

	if ( !managesieve_arg_get_string(&args[0], &text) ) {
		client_send_no(client, "Invalid echo tag.");
		return TRUE;
	}

	if ( !MANAGESIEVE_ARG_IS_EOL(&args[1]) ) {
		client_send_command_error(cmd, "Too many arguments.");
		return TRUE;
	}

	resp_code = t_str_new(256);
	str_append(resp_code, "TAG ");
	managesieve_quote_append_string(resp_code, text, FALSE);

	client_send_okresp(client, str_c(resp_code), "Done");
	return TRUE;
}

