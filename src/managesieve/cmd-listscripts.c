/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "sieve.h"
#include "sieve-storage.h"

#include "managesieve-quote.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"

bool cmd_listscripts(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct sieve_storage_list_context *ctx;
	const char *scriptname;
	unsigned int script_count = 0;
	bool active;
	string_t *str;

	/* no arguments */
	if (!client_read_no_args(cmd))
		return FALSE;

	if ((ctx = sieve_storage_list_init(client->storage)) == NULL) {
		client_command_storage_error(
			cmd, "Failed to list scripts");
		return TRUE;
	}

	/* FIXME: This will be quite slow for large script lists. Implement
	 * some buffering to fix this. Wont truely be an issue with managesieve
	 * though.
	 */
	while ((scriptname = sieve_storage_list_next(ctx, &active)) != NULL) {
		T_BEGIN {
			str = t_str_new(128);

			managesieve_quote_append_string(str, scriptname, FALSE);

			if (active)
				str_append(str, " ACTIVE");

			client_send_line(client, str_c(str));
		} T_END;

		script_count++;
	}

	if (sieve_storage_list_deinit(&ctx) < 0) {
		client_command_storage_error(
			cmd, "Failed to list scripts");
		return TRUE;
	}

	struct event_passthrough *e =
		client_command_create_finish_event(cmd);
	e_debug(e->event(), "Listed %u scripts", script_count);

	client_send_ok(client, "Listscripts completed.");
	return TRUE;
}
