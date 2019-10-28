/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "strfuncs.h"

#include "sieve.h"
#include "sieve-storage.h"

#include "managesieve-client.h"
#include "managesieve-quota.h"

uint64_t managesieve_quota_max_script_size(struct client *client)
{
	return sieve_storage_quota_max_script_size(client->storage);
}

bool managesieve_quota_check_validsize(struct client_command_context *cmd,
				       size_t size)
{
	struct client *client = cmd->client;
	uint64_t limit;

	if (!sieve_storage_quota_validsize(client->storage, size, &limit)) {
		const char *error_msg;

		error_msg = t_strdup_printf(
			"Script is too large (max %llu bytes).",
			(unsigned long long int)limit);

		struct event_passthrough *e =
			client_command_create_finish_event(cmd);
		e_debug(e->event(),
			"Script size check failed "
			"(size %"PRIuSIZE_T" bytes): %s",
			size, error_msg);

		client_send_noresp(client, "QUOTA/MAXSIZE", error_msg);
		return FALSE;
	}
	return TRUE;
}

bool managesieve_quota_check_all(struct client_command_context *cmd,
				 const char *scriptname, size_t size)
{
	struct client *client = cmd->client;
	enum sieve_storage_quota quota;
	uint64_t limit;
	const char *resp_code = NULL, *error_msg = NULL;
	int ret;

	ret = sieve_storage_quota_havespace(client->storage, scriptname,
					    size, &quota, &limit);
	if (ret > 0)
		return TRUE;
	if (ret < 0) {
		client_command_storage_error(
			cmd, "Failed to check quota for script `%s' "
			     "(size %"PRIuSIZE_T" bytes)", scriptname, size);
		return FALSE;
	}

	switch (quota) {
	case SIEVE_STORAGE_QUOTA_MAXSIZE:
		resp_code = "QUOTA/MAXSIZE";
		error_msg = t_strdup_printf("Script is too large "
					    "(max %llu bytes).",
					    (unsigned long long int)limit);
		break;
	case SIEVE_STORAGE_QUOTA_MAXSCRIPTS:
		resp_code = "QUOTA/MAXSCRIPTS";
		error_msg = t_strdup_printf("Script count quota exceeded "
					    "(max %llu scripts).",
					    (unsigned long long int)limit);
		break;
	case SIEVE_STORAGE_QUOTA_MAXSTORAGE:
		resp_code = "QUOTA/MAXSTORAGE";
		error_msg = t_strdup_printf("Script storage quota exceeded "
					    "(max %llu bytes).",
					    (unsigned long long int)limit);
		break;
	default:
		resp_code = "QUOTA";
		error_msg = "Quota exceeded.";
	}

	struct event_passthrough *e =
		client_command_create_finish_event(cmd)->
		add_str("error", error_msg);
	e_debug(e->event(),
		"Quota check failed for script `%s' "
		"(size %"PRIuSIZE_T" bytes): %s",
		scriptname, size, error_msg);

	client_send_noresp(client, resp_code, error_msg);

	return FALSE;
}

