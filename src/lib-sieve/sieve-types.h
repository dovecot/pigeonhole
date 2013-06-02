/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_TYPES_H
#define __SIEVE_TYPES_H

#include "lib.h"

#include <stdio.h>

/*
 * Forward declarations
 */

struct sieve_instance;
struct sieve_callbacks;

struct sieve_script;
struct sieve_binary;

struct sieve_message_data;
struct sieve_script_env;
struct sieve_exec_status;

/*
 * System environment
 */

enum sieve_flag {
	/* Relative paths are resolved to HOME */
	SIEVE_FLAG_HOME_RELATIVE = (1 << 0),
};

/* Sieve evaluation can be performed at various different points as messages
   are processed. */
enum sieve_env_location {
	/* Unknown */
	SIEVE_ENV_LOCATION_UNKNOWN = 0,
	/* "MDA" - evaluation is being performed by a Mail Delivery Agent */
	SIEVE_ENV_LOCATION_MDA,
	/* "MTA" - the Sieve script is being evaluated by a Message Transfer Agent */
	SIEVE_ENV_LOCATION_MTA,
	/* "MS"  - evaluation is being performed by a Message Store */
	SIEVE_ENV_LOCATION_MS
};

/* The point relative to final delivery where the Sieve script is being
   evaluated. */
enum sieve_delivery_phase {
	SIEVE_DELIVERY_PHASE_UNKNOWN = 0,
	SIEVE_DELIVERY_PHASE_PRE,
	SIEVE_DELIVERY_PHASE_DURING,
	SIEVE_DELIVERY_PHASE_POST,
};

struct sieve_environment {
	const char *hostname;
	const char *domainname;

	const char *base_dir;
	const char *username;
	const char *home_dir;

	enum sieve_flag flags;
	enum sieve_env_location location;
	enum sieve_delivery_phase delivery_phase;
};

/*
 * Callbacks
 */

struct sieve_callbacks {
	const char *(*get_homedir)(void *context);
	const char *(*get_setting)(void *context, const char *identifier);
};

/*
 * Errors
 */

enum sieve_error {
	SIEVE_ERROR_NONE = 0,

	/* Temporary internal error */
	SIEVE_ERROR_TEMP_FAILURE,
	/* It's not possible to do the wanted operation */
	SIEVE_ERROR_NOT_POSSIBLE,
	/* Invalid parameters (eg. script name not valid) */
	SIEVE_ERROR_BAD_PARAMS,
	/* No permission to do the request */
	SIEVE_ERROR_NO_PERMISSION,
	/* Out of disk space */
	SIEVE_ERROR_NO_SPACE,
	/* Out of disk space */
	SIEVE_ERROR_NO_QUOTA,
	/* Item (e.g. script or binary) cannot be found */
	SIEVE_ERROR_NOT_FOUND,
	/* Item (e.g. script or binary) already exists */
	SIEVE_ERROR_EXISTS,
	/* Referenced item (e.g. script or binary) is not valid or currupt */
	SIEVE_ERROR_NOT_VALID,
	/* Not allowed to perform the operation because the item is in active use */
	SIEVE_ERROR_ACTIVE
};

/*
 * Compile flags
 */

enum sieve_compile_flags {
	/* No global extensions are allowed
	 *  (as marked by sieve_global_extensions setting)
	 */
	SIEVE_COMPILE_FLAG_NOGLOBAL = (1<<0),
	/* Script is being uploaded (usually through ManageSieve) */
	SIEVE_COMPILE_FLAG_UPLOADED = (1<<1),
	/* Script is being activated (usually through ManageSieve) */
	SIEVE_COMPILE_FLAG_ACTIVATED = (1<<2),
};

/*
 * Message data
 *
 * - The mail message + envelope data
 */

struct sieve_message_data {
	struct mail *mail;
	const char *return_path;
	const char *orig_envelope_to;
	const char *final_envelope_to;
	const char *auth_user;
	const char *id;
};

/*
 * Runtime flags
 */

enum sieve_runtime_flags {
	/* No global extensions are allowed
	 *  (as marked by sieve_global_extensions setting)
	 */
	SIEVE_RUNTIME_FLAG_NOGLOBAL = (1<<0)
};

/*
 * Runtime trace settings
 */

typedef enum {
	SIEVE_TRLVL_NONE,
	SIEVE_TRLVL_ACTIONS,
	SIEVE_TRLVL_COMMANDS,
	SIEVE_TRLVL_TESTS,
	SIEVE_TRLVL_MATCHING
} sieve_trace_level_t;

enum {
	SIEVE_TRFLG_DEBUG = (1 << 0),
	SIEVE_TRFLG_ADDRESSES = (1 << 1)
};

struct sieve_trace_config {
	sieve_trace_level_t level;
	unsigned int flags;
};

/*
 * Script environment
 *
 * - Environment for currently executing script
 */

struct sieve_script_env {
	/* Logging related */
	const char *action_log_format;

	/* Mail-related */
	struct mail_user *user;
	const char *default_mailbox;
	const char *postmaster_address;
	bool mailbox_autocreate;
	bool mailbox_autosubscribe;

	/* External context data */

	void *script_context;

	/* Callbacks */

	/* Interface for sending mail */
	void *(*smtp_open)
		(const struct sieve_script_env *senv, const char *destination,
			const char *return_path, struct ostream **output_r);
	bool (*smtp_close)(const struct sieve_script_env *senv, void *handle);

	/* Interface for marking and checking duplicates */
	int (*duplicate_check)
		(const struct sieve_script_env *senv, const void *id, size_t id_size);
	void (*duplicate_mark)
		(const struct sieve_script_env *senv, const void *id, size_t id_size,
			time_t time);

	/* Interface for rejecting mail */
	int (*reject_mail)(const struct sieve_script_env *senv, const char *recipient,
			const char *reason);

	/* Execution status record */
	struct sieve_exec_status *exec_status;

	/* Runtime trace*/
	struct ostream *trace_stream;
	struct sieve_trace_config trace_config;
};

#define SIEVE_SCRIPT_DEFAULT_MAILBOX(senv) \
	(senv->default_mailbox == NULL ? "INBOX" : senv->default_mailbox )

/*
 * Script execution status
 */

struct sieve_exec_status {
	bool message_saved;
	bool message_forwarded;
	bool tried_default_save;
	bool keep_original;
	struct mail_storage *last_storage;
};

/*
 * Execution exit codes
 */

enum sieve_execution_exitcode {
	SIEVE_EXEC_OK           = 1,
	SIEVE_EXEC_FAILURE      = 0,
	SIEVE_EXEC_TEMP_FAILURE = -1,
	SIEVE_EXEC_BIN_CORRUPT  = -2,
	SIEVE_EXEC_KEEP_FAILED  = -3
};

#endif /* __SIEVE_TYPES_H */
