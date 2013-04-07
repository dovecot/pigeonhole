/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SCRIPT_CLIENT_H
#define __SCRIPT_CLIENT_H

struct script_client;

struct script_client_settings {
	unsigned int client_connect_timeout_msecs;
	unsigned int input_idle_timeout_secs;
	bool debug;
};

struct script_client *script_client_local_create
	(const char *bin_path, const char *const *args,
		const struct script_client_settings *set);
struct script_client *script_client_remote_create
	(const char *socket_path, const char *const *args,
		const struct script_client_settings *set, bool noreply);

void script_client_destroy(struct script_client **_sclient);

void script_client_set_input
	(struct script_client *sclient, struct istream *input);
void script_client_set_output
	(struct script_client *sclient, struct ostream *output);

void script_client_set_env
	(struct script_client *sclient, const char *name, const char *value);

int script_client_run(struct script_client *sclient);

#endif

