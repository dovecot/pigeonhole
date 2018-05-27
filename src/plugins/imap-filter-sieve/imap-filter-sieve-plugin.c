/* Copyright (c) 2017-2018 Pigeonhole authors, see the included COPYING file */

#include "imap-common.h"
#include "str.h"

#include "imap-filter-sieve.h"
#include "imap-filter-sieve-plugin.h"

static struct module *imap_filter_sieve_module;
static imap_client_created_func_t *next_hook_client_created;

/*
 * Client
 */

static void imap_filter_sieve_plugin_client_created(struct client **clientp)
{
	struct client *client = *clientp;
	struct mail_user *user = client->user;

	if (mail_user_is_plugin_loaded(user, imap_filter_sieve_module)) {
		client_add_capability(client, "FILTER=SIEVE");

		imap_filter_sieve_client_created(client);
	}

	if (next_hook_client_created != NULL)
		next_hook_client_created(clientp);
}

/*
 * Plugin
 */

const char *imap_filter_sieve_plugin_version = DOVECOT_ABI_VERSION;
const char imap_filter_sieve_plugin_binary_dependency[] = "imap";

void imap_filter_sieve_plugin_init(struct module *module)
{
	command_register("FILTER", cmd_filter, COMMAND_FLAG_USES_SEQS);
	command_register("UID FILTER", cmd_filter, COMMAND_FLAG_BREAKS_SEQS);

	imap_filter_sieve_module = module;
	next_hook_client_created = imap_client_created_hook_set(
		imap_filter_sieve_plugin_client_created);
	imap_filter_sieve_init(module);
}

void imap_filter_sieve_plugin_deinit(void)
{
	command_unregister("FILTER");
	command_unregister("UID FILTER");

	imap_filter_sieve_deinit();
	imap_client_created_hook_set(next_hook_client_created);
}
