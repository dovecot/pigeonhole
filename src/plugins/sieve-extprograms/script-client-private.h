/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SCRIPT_CLIENT_PRIVATE_H
#define __SCRIPT_CLIENT_PRIVATE_H

#include "script-client.h"

enum script_client_error {
	SCRIPT_CLIENT_ERROR_NONE,
	SCRIPT_CLIENT_ERROR_CONNECT_TIMEOUT,
	SCRIPT_CLIENT_ERROR_RUN_TIMEOUT,
	SCRIPT_CLIENT_ERROR_IO,
	SCRIPT_CLIENT_ERROR_UNKNOWN
};

struct script_client {
	pool_t pool;
	const struct script_client_settings *set;

	char *path;
	const char **args;
	ARRAY_TYPE(const_string) envs;

	int fd_in, fd_out;
	struct io *io;
	struct ioloop *ioloop;
	struct timeout *to;
	time_t start_time;

	struct istream *input, *script_input;
	struct ostream *output, *script_output;

	enum script_client_error error;
	int exit_code;

	int (*connect)(struct script_client *sclient);
	int (*close_output)(struct script_client *sclient);
	int (*disconnect)(struct script_client *sclient, bool force);
	void (*failure)
		(struct script_client *sclient, enum script_client_error error);
	
	unsigned int debug:1;
	unsigned int disconnected:1;
};

void script_client_init
	(struct script_client *sclient, pool_t pool, const char *path,
		const char *const *args, const struct script_client_settings *set);

void script_client_init_streams(struct script_client *sclient);

int script_client_script_connected(struct script_client *sclient);

void script_client_fail
	(struct script_client *sclient, enum script_client_error error);

#endif

