/* Copyright (c) 2002-2010 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "managesieve-quote.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"

#include <stdlib.h>

bool cmd_noop(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct managesieve_arg *args;
	const char *text;
	string_t *resp_code;
	int ret;

	/* [<echo string>] */
	if (!(ret=client_read_args(cmd, 0, 0, &args)))
		return FALSE;

	if ( ret > 1 ) {
		client_send_no(client, "Too many arguments");
		return TRUE;
	}

	if ( args[0].type == MANAGESIEVE_ARG_EOL ) {
		client_send_ok(client, "NOOP Completed");
		return TRUE;
	}

	if ( (text = managesieve_arg_string(&args[0])) == NULL ) {
		client_send_no(client, "Invalid echo tag.");
		return TRUE;
	}

	resp_code = t_str_new(256);
	str_append(resp_code, "TAG ");
	managesieve_quote_append_string(resp_code, text, FALSE);

	client_send_okresp(client, str_c(resp_code), "Done");
	return TRUE;
}

