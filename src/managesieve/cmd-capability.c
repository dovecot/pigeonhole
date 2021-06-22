/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "strfuncs.h"
#include "ostream.h"

#include "sieve.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"

static void send_capability(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	const char *sieve_cap, *notify_cap;
	unsigned int max_redirects;

	/* Get capabilities */
	sieve_cap = sieve_get_capabilities(client->svinst, NULL);
	notify_cap = sieve_get_capabilities(client->svinst, "notify");
	max_redirects = sieve_max_redirects(client->svinst);

	/* Default capabilities */
	client_send_line(client,
		t_strconcat(
			"\"IMPLEMENTATION\" \"",
			client->set->managesieve_implementation_string,
			"\"", NULL));
	client_send_line(client,
		t_strconcat(
			"\"SIEVE\" \"",
			(sieve_cap == NULL ? "" : sieve_cap),
			"\"", NULL));

	/* Maximum number of redirects (if limited) */
	if (max_redirects > 0) {
		client_send_line(client,
			t_strdup_printf("\"MAXREDIRECTS\" \"%u\"",
					max_redirects));
	}

	/* Notify methods */
	if (notify_cap != NULL) {
		client_send_line(client,
			t_strconcat("\"NOTIFY\" \"", notify_cap, "\"",
				    NULL));
	}

	/* Protocol version */
	client_send_line(client, "\"VERSION\" \"1.0\"");
}

bool cmd_capability(struct client_command_context *cmd)
{
	struct client *client = cmd->client;

	/* no arguments */
	if (!client_read_no_args(cmd))
		return FALSE;

	o_stream_cork(client->output);

	T_BEGIN {
		send_capability(cmd);
	} T_END;

	client_send_line(client, "OK \"Capability completed.\"");

	o_stream_uncork(client->output);

	return TRUE;

}
