#ifndef SIEVE_TYPES_H
#define SIEVE_TYPES_H

#include "lib.h"
#include "smtp-address.h"

#include <stdio.h>

/*
 * Forward declarations
 */

struct smtp_params_mail;
struct smtp_params_rcpt;

struct sieve_instance;
struct sieve_callbacks;

struct sieve_script;
struct sieve_binary;

struct sieve_message_data;
struct sieve_script_env;
struct sieve_exec_status;
struct sieve_trace_log;

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
	/* "MTA" - the Sieve script is being evaluated by a Message Transfer
	   Agent */
	SIEVE_ENV_LOCATION_MTA,
	/* "MS"  - evaluation is being performed by a Message Store */
	SIEVE_ENV_LOCATION_MS,
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
	const char *temp_dir;

	struct event *event_parent;

	enum sieve_flag flags;
	enum sieve_env_location location;
	enum sieve_delivery_phase delivery_phase;
};

/*
 * Callbacks
 */

struct sieve_callbacks {
	const char *
	(*get_homedir)(struct sieve_instance *svinst, void *context);
	const char *
	(*get_setting)(struct sieve_instance *svinst, void *context,
		       const char *identifier);
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
	SIEVE_ERROR_NO_QUOTA,
	/* Item (e.g. script or binary) cannot be found */
	SIEVE_ERROR_NOT_FOUND,
	/* Item (e.g. script or binary) already exists */
	SIEVE_ERROR_EXISTS,
	/* Referenced item (e.g. script or binary) is not valid or currupt */
	SIEVE_ERROR_NOT_VALID,
	/* Not allowed to perform the operation because the item is in active
	  use */
	SIEVE_ERROR_ACTIVE,
	/* Operation exceeds resource limit */
	SIEVE_ERROR_RESOURCE_LIMIT,
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
	/* Compiled for environment with no access to envelope */
	SIEVE_COMPILE_FLAG_NO_ENVELOPE = (1<<3),
};

/*
 * Message data
 *
 * - The mail message + envelope data
 */

struct sieve_message_data {
	struct mail *mail;

	const char *auth_user;
	const char *id;

	struct {
		const struct smtp_address *mail_from;
		const struct smtp_params_mail *mail_params;

		const struct smtp_address *rcpt_to;
		const struct smtp_params_rcpt *rcpt_params;
	} envelope;
};

/*
 * Runtime flags
 */

enum sieve_execute_flags {
	/* No global extensions are allowed
	 *  (as marked by sieve_global_extensions setting)
	 */
	SIEVE_EXECUTE_FLAG_NOGLOBAL = (1<<0),
	/* Do not execute (implicit keep) at the end */
	SIEVE_EXECUTE_FLAG_DEFER_KEEP = (1<<1),
	/* There is no envelope */
	SIEVE_EXECUTE_FLAG_NO_ENVELOPE = (1<<2),
	/* Skip sending responses */
	SIEVE_EXECUTE_FLAG_SKIP_RESPONSES = (1<<3),
	/* Log result as info (when absent, only debug logging is performed) */
	SIEVE_EXECUTE_FLAG_LOG_RESULT = (1<<4),
};

/*
 * Runtime trace settings
 */

typedef enum {
	SIEVE_TRLVL_NONE = 0,
	SIEVE_TRLVL_ACTIONS,
	SIEVE_TRLVL_COMMANDS,
	SIEVE_TRLVL_TESTS,
	SIEVE_TRLVL_MATCHING,
} sieve_trace_level_t;

enum {
	SIEVE_TRFLG_DEBUG = (1 << 0),
	SIEVE_TRFLG_ADDRESSES = (1 << 1),
};

struct sieve_trace_config {
	sieve_trace_level_t level;
	unsigned int flags;
};

/*
 * Duplicate checking
 */

enum sieve_duplicate_check_result {
	SIEVE_DUPLICATE_CHECK_RESULT_EXISTS = 1,
	SIEVE_DUPLICATE_CHECK_RESULT_NOT_FOUND = 0,
	SIEVE_DUPLICATE_CHECK_RESULT_FAILURE = -1,
	SIEVE_DUPLICATE_CHECK_RESULT_TEMP_FAILURE = -2,
};

/*
 * Script environment
 *
 * - Environment for currently executing script
 */

struct sieve_script_env {
	/* Mail-related */
	struct mail_user *user;
	const struct message_address *postmaster_address;
	const char *default_mailbox;
	bool mailbox_autocreate;
	bool mailbox_autosubscribe;

	/* External context data */

	void *script_context;

	/* Callbacks */

	/* Interface for sending mail */
	void *(*smtp_start)(const struct sieve_script_env *senv,
			    const struct smtp_address *mail_from);
	/* Add a new recipient */
	void (*smtp_add_rcpt)(const struct sieve_script_env *senv, void *handle,
			      const struct smtp_address *rcpt_to);
	/* Get an output stream where the message can be written to. The
	   recipients  must already be added before calling this. */
	struct ostream *(*smtp_send)(const struct sieve_script_env *senv,
				     void *handle);
	/* Abort the SMTP transaction after smtp_send() is already issued */
	void (*smtp_abort)(const struct sieve_script_env *senv, void *handle);
	/* Returns 1 on success, 0 on permanent failure, -1 on temporary failure. */
	int (*smtp_finish)(const struct sieve_script_env *senv, void *handle,
			   const char **error_r);

	/* Interface for marking and checking duplicates */
	void *(*duplicate_transaction_begin)(
		const struct sieve_script_env *senv);
	void (*duplicate_transaction_commit)(void **_dup_trans);
	void (*duplicate_transaction_rollback)(void **_dup_trans);

	enum sieve_duplicate_check_result
	(*duplicate_check)(void *dup_trans, const struct sieve_script_env *senv,
			   const void *id, size_t id_size);
	void (*duplicate_mark)(void *dup_trans,
			       const struct sieve_script_env *senv,
			       const void *id, size_t id_size, time_t time);

	/* Interface for rejecting mail */
	int (*reject_mail)(const struct sieve_script_env *senv,
			   const struct smtp_address *recipient,
			   const char *reason);

	/* Interface for amending result messages */
	const char *
	(*result_amend_log_message)(const struct sieve_script_env *senv,
				    enum log_type log_type,
				    const char *message);

	/* Execution status record */
	struct sieve_exec_status *exec_status;

	/* Runtime trace*/
	struct sieve_trace_log *trace_log;
	struct sieve_trace_config trace_config;
};

#define SIEVE_SCRIPT_DEFAULT_MAILBOX(senv) \
	(senv->default_mailbox == NULL ? "INBOX" : senv->default_mailbox )

/*
 * Resource usage
 */

struct sieve_resource_usage {
	/* The total amount of system + user CPU time consumed while executing
	   the Sieve script. */
	unsigned int cpu_time_msecs;
};

/*
 * Script execution status
 */

struct sieve_exec_status {
	struct mail_storage *last_storage;

	struct sieve_resource_usage resource_usage;

	bool message_saved:1;
	bool message_forwarded:1;
	bool tried_default_save:1;
	bool keep_original:1;
	bool store_failed:1;
	bool significant_action_executed:1;
};

/*
 * Execution exit codes
 */

enum sieve_execution_exitcode {
	SIEVE_EXEC_OK         	        = 1,
	SIEVE_EXEC_FAILURE              = 0,
	SIEVE_EXEC_TEMP_FAILURE         = -1,
	SIEVE_EXEC_BIN_CORRUPT          = -2,
	SIEVE_EXEC_KEEP_FAILED          = -3,
	SIEVE_EXEC_RESOURCE_LIMIT       = -4,
};

#endif
