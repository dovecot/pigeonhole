/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
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

	/* <scrip name>*/
	if ( !client_read_string_args(cmd, TRUE, 1, &scriptname) )
		return FALSE;

	script = sieve_storage_open_script
		(storage, scriptname, NULL);
	if ( script == NULL ) {
		client_send_storage_error(client, storage);
		return TRUE;
	}

	if ( sieve_script_delete(script, FALSE) < 0 ) {
		client_send_storage_error(client, storage);
	} else {
		client->deleted_count++;
		client_send_ok(client, "Deletescript completed.");
	}

	sieve_script_unref(&script);
	return TRUE;
}
