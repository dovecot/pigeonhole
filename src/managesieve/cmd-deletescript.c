/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"

bool cmd_deletescript(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct sieve_storage *storage = client->storage;
	const char *scriptname;
	struct sieve_script *script;

	/* <script name>*/
	if (!client_read_string_args(cmd, TRUE, 1, &scriptname))
		return FALSE;

	event_add_str(cmd->event, "script_name", scriptname);

	script = sieve_storage_open_script(storage, scriptname, NULL);
	if (script == NULL || sieve_script_delete(script, FALSE) < 0) {
		client_command_storage_error(
			cmd, "Failed to delete script `%s'", scriptname);
		sieve_script_unref(&script);
		return TRUE;
	}

	struct event_passthrough *e =
		client_command_create_finish_event(cmd);
	e_debug(e->event(), "Deleted script `%s'", scriptname);

	client->deleted_count++;
	client_send_ok(client, "Deletescript completed.");

	sieve_script_unref(&script);
	return TRUE;
}
