/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"

bool cmd_renamescript(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct sieve_storage *storage = client->storage;
	const char *scriptname, *newname;
	struct sieve_script *script;

	/* <oldname> <newname> */
	if (!client_read_string_args(cmd, TRUE, 2, &scriptname, &newname))
		return FALSE;

	event_add_str(cmd->event, "old_script_name", scriptname);
	event_add_str(cmd->event, "new_script_name", newname);

	script = sieve_storage_open_script(storage, scriptname, NULL);
	if (script == NULL) {
		client_command_storage_error(
			cmd, "Failed to open script `%s' for rename to `%s'",
			scriptname, newname);
		return TRUE;
	}

	if (sieve_script_rename(script, newname) < 0) {
		client_command_storage_error(
			cmd, "Failed to rename script `%s' to `%s'",
			scriptname, newname);
	} else {
		client->renamed_count++;

		struct event_passthrough *e =
			client_command_create_finish_event(cmd);
		e_debug(e->event(), "Renamed script `%s' to `%s'",
			scriptname, newname);

		client_send_ok(client, "Renamescript completed.");
	}

	sieve_script_unref(&script);
	return TRUE;
}

