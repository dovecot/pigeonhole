/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
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

	script = sieve_storage_open_script
		(storage, scriptname, NULL);
	if (script == NULL) {
		client_send_storage_error(client, storage);
		return TRUE;
	}

	if (sieve_script_rename(script, newname) < 0) {
		client_send_storage_error(client, storage);
	} else {
		client->renamed_count++;
		client_send_ok(client, "Renamescript completed.");
	}

	sieve_script_unref(&script);
	return TRUE;
}

