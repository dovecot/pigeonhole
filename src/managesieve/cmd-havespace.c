/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "managesieve-client.h"
#include "managesieve-quota.h"

bool cmd_havespace(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	const struct managesieve_arg *args;
	const char *scriptname;
	uoff_t size;

	/* <scriptname> <size> */
	if (!client_read_args(cmd, 2, 0, TRUE, &args))
		return FALSE;

	if (!managesieve_arg_get_string(&args[0], &scriptname)) {
		client_send_no(client, "Invalid string for scriptname.");
		return TRUE;
	}

	if (!managesieve_arg_get_number(&args[1], &size)) {
		client_send_no(client, "Invalid scriptsize argument.");
		return TRUE;
	}

	if (!sieve_script_name_is_valid(scriptname)) {
		client_send_no(client, "Invalid script name.");
		return TRUE;
	}

	if (size == 0) {
		client_send_no(client, "Cannot upload empty script.");
		return TRUE;
	}

	event_add_str(cmd->event, "script_name", scriptname);
	event_add_int(cmd->event, "script_size", size);

	if (!managesieve_quota_check_all(cmd, scriptname, size))
		return TRUE;

	struct event_passthrough *e =
		client_command_create_finish_event(cmd);
	e_debug(e->event(), "Quota is within limits for script `%s' "
		"with size %"PRIuSIZE_T, scriptname, size);

	client_send_ok(client, "Putscript would succeed.");
	return TRUE;
}
