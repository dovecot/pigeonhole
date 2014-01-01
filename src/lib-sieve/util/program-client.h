/* Copyright (c) 2002-2014 Pigeonhole authors, see the included COPYING file
 */

#ifndef __PROGRAM_CLIENT_H
#define __PROGRAM_CLIENT_H

struct program_client;

struct program_client_settings {
	unsigned int client_connect_timeout_msecs;
	unsigned int input_idle_timeout_secs;
	bool debug;
};

struct program_client *program_client_local_create
	(const char *bin_path, const char *const *args,
		const struct program_client_settings *set);
struct program_client *program_client_remote_create
	(const char *socket_path, const char *const *args,
		const struct program_client_settings *set, bool noreply);

void program_client_destroy(struct program_client **_pclient);

void program_client_set_input
	(struct program_client *pclient, struct istream *input);
void program_client_set_output
	(struct program_client *pclient, struct ostream *output);

void program_client_set_output_seekable
	(struct program_client *pclient, const char *temp_prefix);
struct istream *program_client_get_output_seekable
	(struct program_client *pclient);

void program_client_set_env
	(struct program_client *pclient, const char *name, const char *value);

int program_client_run(struct program_client *pclient);

#endif

