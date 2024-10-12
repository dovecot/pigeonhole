/* Copyright (c) 2016-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "home-expand.h"
#include "smtp-address.h"
#include "smtp-submit.h"
#include "mail-storage.h"
#include "mail-user.h"
#include "mail-duplicate.h"
#include "imap-client.h"
#include "imap-settings.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "ext-imapsieve-common.h"

#include "imap-sieve.h"

/*
 * Configuration
 */

#define DUPLICATE_DB_NAME "lda-dupes"
#define IMAP_SIEVE_MAX_USER_ERRORS 30

/*
 * IMAP Sieve
 */

struct imap_sieve {
	pool_t pool;
	struct client *client;
	const char *home_dir;

	struct sieve_instance *svinst;
	struct sieve_storage *storage;

	const struct sieve_extension *ext_imapsieve;
	const struct sieve_extension *ext_vnd_imapsieve;

	struct mail_duplicate_db *dup_db;

	struct sieve_error_handler *master_ehandler;
};

static const char *
mail_sieve_get_setting(struct sieve_instance *svinst ATTR_UNUSED, void *context,
		       const char *identifier)
{
	struct imap_sieve *isieve = context;
	struct mail_user *user = isieve->client->user;

	return mail_user_plugin_getenv(user, identifier);
}

static const struct sieve_callbacks mail_sieve_callbacks = {
	NULL,
	mail_sieve_get_setting
};

struct imap_sieve *imap_sieve_init(struct client *client)
{
	struct sieve_environment svenv;
	struct imap_sieve *isieve;
	struct mail_user *user = client->user;
	bool debug = user->set->mail_debug;
	pool_t pool;

	pool = pool_alloconly_create("imap_sieve", 256);
	isieve = p_new(pool, struct imap_sieve, 1);
	isieve->pool = pool;
	isieve->client = client;

	isieve->dup_db = mail_duplicate_db_init(user, DUPLICATE_DB_NAME);

	i_zero(&svenv);
	svenv.username = user->username;
	(void)mail_user_get_home(user, &svenv.home_dir);
	svenv.hostname = user->set->hostname;
	svenv.base_dir = user->set->base_dir;
	svenv.event_parent = client->event;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;
	svenv.location = SIEVE_ENV_LOCATION_MS;
	svenv.delivery_phase = SIEVE_DELIVERY_PHASE_POST;

	isieve->home_dir = p_strdup(pool, svenv.home_dir);

	if (sieve_init(&svenv, &mail_sieve_callbacks, isieve,
		       debug, &isieve->svinst) < 0)
		return isieve;

	if (sieve_extension_replace(isieve->svinst, &imapsieve_extension,
				    TRUE, &isieve->ext_imapsieve) < 0 ||
	    sieve_extension_replace(isieve->svinst, &vnd_imapsieve_extension,
				    TRUE, &isieve->ext_vnd_imapsieve) < 0) {
		sieve_deinit(&isieve->svinst);
		return isieve;
	}

	isieve->master_ehandler =
		sieve_master_ehandler_create(isieve->svinst, 0);
	sieve_error_handler_accept_infolog(isieve->master_ehandler, TRUE);
	sieve_error_handler_accept_debuglog(isieve->master_ehandler, debug);

	return isieve;
}

void imap_sieve_deinit(struct imap_sieve **_isieve)
{
	struct imap_sieve *isieve = *_isieve;

	*_isieve = NULL;

	sieve_error_handler_unref(&isieve->master_ehandler);

	sieve_storage_unref(&isieve->storage);
	sieve_extension_unregister(isieve->ext_imapsieve);
	sieve_extension_unregister(isieve->ext_vnd_imapsieve);
	sieve_deinit(&isieve->svinst);

	mail_duplicate_db_deinit(&isieve->dup_db);

	pool_unref(&isieve->pool);
}

static int
imap_sieve_get_storage(struct imap_sieve *isieve,
		       struct sieve_storage **storage_r)
{
	enum sieve_storage_flags storage_flags = 0;
	struct mail_user *user = isieve->client->user;
	enum sieve_error error_code;

	if (isieve->storage != NULL) {
		*storage_r = isieve->storage;
		return 1;
	}

	// FIXME: limit interval between retries

	if (isieve->svinst == NULL) {
		*storage_r = NULL;
		return -1;
	}

	if (sieve_storage_create_personal(isieve->svinst, user, storage_flags,
					  &isieve->storage, &error_code) < 0) {
		if (error_code == SIEVE_ERROR_TEMP_FAILURE)
			return -1;
		return 0;
	}
	*storage_r = isieve->storage;
	return 1;
}

/*
 * Mail transmission
 */

static void *
imap_sieve_smtp_start(const struct sieve_script_env *senv,
		      const struct smtp_address *mail_from)
{
	struct imap_sieve_context *isctx = senv->script_context;
	struct imap_sieve *isieve = isctx->isieve;
	const struct smtp_submit_settings *smtp_set = isieve->client->smtp_set;
	struct smtp_submit_input submit_input;

	i_zero(&submit_input);

	return smtp_submit_init_simple(&submit_input, smtp_set, mail_from);
}

static void
imap_sieve_smtp_add_rcpt(const struct sieve_script_env *senv ATTR_UNUSED,
			 void *handle, const struct smtp_address *rcpt_to)
{
	struct smtp_submit *smtp_submit = handle;

	smtp_submit_add_rcpt(smtp_submit, rcpt_to);
}

static struct ostream *
imap_sieve_smtp_send(const struct sieve_script_env *senv ATTR_UNUSED,
		     void *handle)
{
	struct smtp_submit *smtp_submit = handle;

	return smtp_submit_send(smtp_submit);
}

static void
imap_sieve_smtp_abort(const struct sieve_script_env *senv ATTR_UNUSED,
		      void *handle)
{
	struct smtp_submit *smtp_submit = handle;

	smtp_submit_deinit(&smtp_submit);
}

static int
imap_sieve_smtp_finish(const struct sieve_script_env *senv ATTR_UNUSED,
		       void *handle, const char **error_r)
{
	struct smtp_submit *smtp_submit = handle;
	int ret;

	ret = smtp_submit_run(smtp_submit, error_r);
	smtp_submit_deinit(&smtp_submit);
	return ret;
}

/*
 * Duplicate checking
 */

static void *
imap_sieve_duplicate_transaction_begin(const struct sieve_script_env *senv)
{
	struct imap_sieve_context *isctx = senv->script_context;

	return mail_duplicate_transaction_begin(isctx->isieve->dup_db);
}

static void imap_sieve_duplicate_transaction_commit(void **_dup_trans)
{
	struct mail_duplicate_transaction *dup_trans = *_dup_trans;

	*_dup_trans = NULL;
	mail_duplicate_transaction_commit(&dup_trans);
}

static void imap_sieve_duplicate_transaction_rollback(void **_dup_trans)
{
	struct mail_duplicate_transaction *dup_trans = *_dup_trans;

	*_dup_trans = NULL;
	mail_duplicate_transaction_rollback(&dup_trans);
}

static enum sieve_duplicate_check_result
imap_sieve_duplicate_check(void *_dup_trans,
			   const struct sieve_script_env *senv,
			   const void *id, size_t id_size)
{
	struct mail_duplicate_transaction *dup_trans = _dup_trans;

	switch (mail_duplicate_check(dup_trans, id, id_size,
				     senv->user->username)) {
	case MAIL_DUPLICATE_CHECK_RESULT_EXISTS:
		return SIEVE_DUPLICATE_CHECK_RESULT_EXISTS;
	case MAIL_DUPLICATE_CHECK_RESULT_NOT_FOUND:
		return SIEVE_DUPLICATE_CHECK_RESULT_NOT_FOUND;
	case MAIL_DUPLICATE_CHECK_RESULT_DEADLOCK:
	case MAIL_DUPLICATE_CHECK_RESULT_LOCK_TIMEOUT:
		return SIEVE_DUPLICATE_CHECK_RESULT_TEMP_FAILURE;
	case MAIL_DUPLICATE_CHECK_RESULT_IO_ERROR:
	case MAIL_DUPLICATE_CHECK_RESULT_TOO_MANY_LOCKS:
		break;
	}
	return SIEVE_DUPLICATE_CHECK_RESULT_FAILURE;
}

static void
imap_sieve_duplicate_mark(void *_dup_trans, const struct sieve_script_env *senv,
			  const void *id, size_t id_size, time_t time)
{
	struct mail_duplicate_transaction *dup_trans = _dup_trans;

	mail_duplicate_mark(dup_trans, id, id_size, senv->user->username, time);
}

/*
 * Result logging
 */

static const char *
imap_sieve_result_amend_log_message(const struct sieve_script_env *senv,
				    enum log_type log_type ATTR_UNUSED,
				    const char *message)
{
	struct imap_sieve_context *isctx = senv->script_context;
	string_t *str;

	if (isctx->mail == NULL)
		return message;

	str = t_str_new(256);
	str_printfa(str, "uid=%u: ", isctx->mail->uid);
	str_append(str, message);
	return str_c(str);
}

/*
 * IMAP Sieve run
 */

struct imap_sieve_run_script {
	struct sieve_script *script;
	struct sieve_binary *binary;

	/* Compile failed once with this error;
	   don't try again for this transaction */
	enum sieve_error compile_error;

	/* This is the user script */
	bool user_script:1;
	/* Binary corrupt after recompile; don't recompile again */
	bool binary_corrupt:1;
	/* Resource usage exceeded */
	bool rusage_exceeded:1;
};

struct imap_sieve_run {
	pool_t pool;
	struct imap_sieve *isieve;
	struct mailbox *dest_mailbox, *src_mailbox;
	char *cause;

	struct sieve_error_handler *user_ehandler;
	char *userlog;

	struct sieve_trace_config trace_config;
	struct sieve_trace_log *trace_log;

	struct imap_sieve_run_script *scripts;
	unsigned int scripts_count;

	bool trace_log_initialized:1;
};

ARRAY_DEFINE_TYPE(imap_sieve_run_script, struct imap_sieve_run_script);

static struct sieve_script *
imap_sieve_run_find_user_script(struct imap_sieve_run *isrun)
{
	unsigned int i;

	for (i = 0; i < isrun->scripts_count; i++) {
		if (isrun->scripts[i].user_script)
			return isrun->scripts[i].script;
	}
	return NULL;
}

static void
imap_sieve_run_init_user_log(struct imap_sieve_run *isrun)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	struct sieve_script *user_script;
	const char *log_path;

	user_script = imap_sieve_run_find_user_script(isrun);
	log_path = sieve_user_get_log_path(svinst, user_script);
	if (log_path != NULL) {
		isrun->userlog = p_strdup(isrun->pool, log_path);
		isrun->user_ehandler = sieve_logfile_ehandler_create(
			svinst, log_path, IMAP_SIEVE_MAX_USER_ERRORS);
	}
}

static void
imap_sieve_run_init_trace_log(struct imap_sieve_run *isrun,
			      struct sieve_trace_config *trace_config_r,
			      struct sieve_trace_log **trace_log_r)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	struct mail_user *user = isieve->client->user;

	if (isrun->trace_log_initialized) {
		*trace_config_r = isrun->trace_config;
		*trace_log_r = isrun->trace_log;
		return;
	}
	isrun->trace_log_initialized = TRUE;

	if (sieve_trace_config_get(svinst, &isrun->trace_config) < 0 ||
	    sieve_trace_log_open(svinst, &isrun->trace_log) < 0) {
		i_zero(&isrun->trace_config);
		isrun->trace_log = NULL;

		i_zero(trace_config_r);
		*trace_log_r = NULL;
		return;
	}

	/* Write header for trace file */
	sieve_trace_log_printf(isrun->trace_log,
		"Sieve trace log for IMAPSIEVE:\n"
		"\n"
		"  Username: %s\n", user->username);
	if (user->session_id != NULL) {
		sieve_trace_log_printf(isrun->trace_log,
			"  Session ID: %s\n", user->session_id);
	}
	if (isrun->src_mailbox != NULL) {
		sieve_trace_log_printf(isrun->trace_log,
				       "  Source mailbox: %s\n",
				       mailbox_get_vname(isrun->src_mailbox));
	}

	sieve_trace_log_printf(isrun->trace_log,
		"  Destination mailbox: %s\n"
		"  Cause: %s\n\n",
		mailbox_get_vname(isrun->dest_mailbox),
		isrun->cause);

	*trace_config_r = isrun->trace_config;
	*trace_log_r = isrun->trace_log;
}

static int
imap_sieve_run_init_scripts(struct imap_sieve *isieve,
			    ARRAY_TYPE(imap_sieve_run_script) *scripts,
			    struct sieve_storage *storage,
			    const char *script_name,
			    const char *const *scripts_before,
			    const char *const *scripts_after)
{
	struct sieve_instance *svinst = isieve->svinst;
	enum sieve_error error_code;
	const char *const *sp;

	/* Admin scripts before user script */
	if (scripts_before != NULL) {
		for (sp = scripts_before; *sp != NULL; sp++) {
			struct sieve_script *script;

			if (sieve_script_create_open(svinst, *sp, NULL, &script,
						     &error_code, NULL) < 0) {
				if (error_code == SIEVE_ERROR_TEMP_FAILURE)
					return -1;
				continue;
			}

			struct imap_sieve_run_script *rscript;

			rscript = array_append_space(scripts);
			rscript->script = script;
		}
	}

	/* The user script */
	if (storage != NULL) {
		struct sieve_script *script;

		if (sieve_storage_open_script(storage, script_name,
					      &script, &error_code) < 0) {
			if (error_code == SIEVE_ERROR_TEMP_FAILURE)
				return -1;
		} else {
			struct imap_sieve_run_script *rscript;

			rscript = array_append_space(scripts);
			rscript->script = script;
			rscript->user_script = TRUE;
		}
	}

	/* Admin scripts after user script */
	if (scripts_after != NULL) {
		for (sp = scripts_after; *sp != NULL; sp++) {
			struct sieve_script *script;

			if (sieve_script_create_open(svinst, *sp, NULL, &script,
						     &error_code, NULL) < 0) {
				if (error_code == SIEVE_ERROR_TEMP_FAILURE)
					return -1;
				continue;
			}

			struct imap_sieve_run_script *rscript;

			rscript = array_append_space(scripts);
			rscript->script = script;
		}
	}

	return 0;
}

int imap_sieve_run_init(struct imap_sieve *isieve,
			struct mailbox *dest_mailbox,
			struct mailbox *src_mailbox,
			const char *cause, const char *script_name,
			const char *const *scripts_before,
			const char *const *scripts_after,
			struct imap_sieve_run **isrun_r)
{
	struct imap_sieve_run *isrun;
	struct sieve_storage *storage;
	ARRAY_TYPE(imap_sieve_run_script) scripts;
	pool_t pool;
	int ret;

	*isrun_r = NULL;
	if (isieve->svinst == NULL)
		return -1;

	/* Get storage for user script */
	storage = NULL;
	if (script_name != NULL && *script_name != '\0' &&
	    (ret = imap_sieve_get_storage(isieve, &storage)) < 0)
		return ret;

	/* Open all scripts */
	pool = pool_alloconly_create("imap_sieve_run", 256);
	p_array_init(&scripts, pool, 16);

	ret = imap_sieve_run_init_scripts(isieve, &scripts,
					  storage, script_name,
					  scripts_before, scripts_after);
	if (ret < 0) {
		struct imap_sieve_run_script *rscript;

		array_foreach_modifiable(&scripts, rscript)
			sieve_script_unref(&rscript->script);
		pool_unref(&pool);
		return -1;
	}
	if (array_is_empty(&scripts)) {
		/* None of the scripts could be opened */
		pool_unref(&pool);
		return 0;
	}

	/* Initialize */
	isrun = p_new(pool, struct imap_sieve_run, 1);
	isrun->pool = pool;
	isrun->isieve = isieve;
	isrun->dest_mailbox = dest_mailbox;
	isrun->src_mailbox = src_mailbox;
	isrun->cause = p_strdup(pool, cause);
	isrun->scripts = array_get_modifiable(&scripts, &isrun->scripts_count);

	imap_sieve_run_init_user_log(isrun);

	*isrun_r = isrun;
	return 1;
}

void imap_sieve_run_deinit(struct imap_sieve_run **_isrun)
{
	struct imap_sieve_run *isrun = *_isrun;
	unsigned int i;

	*_isrun = NULL;

	for (i = 0; i < isrun->scripts_count; i++) {
		if (isrun->scripts[i].binary != NULL)
			sieve_close(&isrun->scripts[i].binary);
		sieve_script_unref(&isrun->scripts[i].script);
	}
	if (isrun->user_ehandler != NULL)
		sieve_error_handler_unref(&isrun->user_ehandler);
	if (isrun->trace_log != NULL)
		sieve_trace_log_free(&isrun->trace_log);

	pool_unref(&isrun->pool);
}

static struct sieve_binary *
imap_sieve_run_open_script(struct imap_sieve_run *isrun,
			   struct imap_sieve_run_script *rscript,
			   enum sieve_compile_flags cpflags,
			   bool recompile, enum sieve_error *error_code_r)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	struct sieve_script *script = rscript->script;
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	const char *compile_name = "compile";
	int ret;

	if (recompile) {
		/* Warn */
		e_warning(sieve_get_event(svinst),
			  "Encountered corrupt binary: re-compiling script '%s'",
			  sieve_script_label(script));
		compile_name = "re-compile";
	} else {
		e_debug(sieve_get_event(svinst),
			"Loading script '%s'", sieve_script_label(script));
	}

	if (rscript->user_script)
		ehandler = isrun->user_ehandler;
	else
		ehandler = isieve->master_ehandler;
	sieve_error_handler_reset(ehandler);

	/* Load or compile the sieve script */
	if (recompile) {
		ret = sieve_compile_script(script, ehandler, cpflags,
					   &sbin, error_code_r);
	} else {
		ret = sieve_open_script(script, ehandler, cpflags,
					&sbin, error_code_r);
	}

	/* Handle error */
	if (ret < 0) {
		switch (*error_code_r) {
		/* Script not found */
		case SIEVE_ERROR_NOT_FOUND:
			e_debug(sieve_get_event(svinst),
				"Script '%s' is missing for %s",
				sieve_script_label(script), compile_name);
			break;
		/* Temporary failure */
		case SIEVE_ERROR_TEMP_FAILURE:
			e_error(sieve_get_event(svinst),
				"Failed to open script '%s' for %s "
				"(temporary failure)",
				sieve_script_label(script), compile_name);
			break;
		/* Compile failed */
		case SIEVE_ERROR_NOT_VALID:
			if (rscript->user_script &&
			   isrun->userlog != NULL ) {
				e_info(sieve_get_event(svinst),
				       "Failed to %s script '%s' "
				       "(view user logfile '%s' for more information)",
				       compile_name, sieve_script_label(script),
				       isrun->userlog);
				break;
			}
			e_error(sieve_get_event(svinst),
				"Failed to %s script '%s'",
				compile_name, sieve_script_label(script));
			break;
		/* Cumulative resource limit exceeded */
		case SIEVE_ERROR_RESOURCE_LIMIT:
			e_error(sieve_get_event(svinst),
				"Failed to open script '%s' for %s "
				"(cumulative resource limit exceeded)",
				sieve_script_label(script), compile_name);
			break;
		/* Something else */
		default:
			e_error(sieve_get_event(svinst),
				"Failed to open script '%s' for %s",
				sieve_script_label(script), compile_name);
			break;
		}

		return NULL;
	}

	if (!recompile)
		(void)sieve_save(sbin, FALSE, NULL);
	return sbin;
}

static int
imap_sieve_handle_exec_status(struct imap_sieve_run *isrun,
			      struct imap_sieve_run_script *rscript, int status,
			      struct sieve_exec_status *estatus, bool *fatal_r)
			      ATTR_NULL(2)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	struct sieve_script *script = rscript->script;
	const char *userlog_notice = "";
	enum log_type log_level, user_log_level;
	enum mail_error mail_error = MAIL_ERROR_NONE;
	int ret = -1;

	*fatal_r = FALSE;

	log_level = user_log_level = LOG_TYPE_ERROR;

	if (estatus->last_storage != NULL && estatus->store_failed) {
		mail_storage_get_last_error(estatus->last_storage, &mail_error);

		/* Don't bother administrator too much with benign errors */
		if (mail_error == MAIL_ERROR_NOQUOTA) {
			log_level = LOG_TYPE_INFO;
			user_log_level = LOG_TYPE_INFO;
		}
	}

	if (rscript->user_script && isrun->userlog != NULL) {
		userlog_notice = t_strdup_printf(
			" (user logfile %s may reveal additional details)",
			isrun->userlog);
		user_log_level = LOG_TYPE_INFO;
	}

	switch (status) {
	case SIEVE_EXEC_FAILURE:
		e_log(sieve_get_event(svinst), user_log_level,
			"Execution of script '%s' failed%s",
			sieve_script_label(script), userlog_notice);
		ret = 0;
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
		e_log(sieve_get_event(svinst), log_level,
		      "Execution of script '%s' was aborted "
		      "due to temporary failure%s",
		      sieve_script_label(script), userlog_notice);
		*fatal_r = TRUE;
		ret = -1;
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
		e_error(sieve_get_event(svinst),
			"!!BUG!!: Binary compiled from '%s' is still corrupt; "
			"bailing out and reverting to default action",
			sieve_script_label(script));
		*fatal_r = TRUE;
		ret = -1;
		break;
	case SIEVE_EXEC_RESOURCE_LIMIT:
		e_error(sieve_get_event(svinst),
			"Execution of script '%s' was aborted "
			"due to excessive resource usage",
			sieve_script_label(script));
		*fatal_r = TRUE;
		ret = -1;
		break;
	case SIEVE_EXEC_KEEP_FAILED:
		e_log(sieve_get_event(svinst), log_level,
		      "Execution of script '%s' failed "
		      "with unsuccessful implicit keep%s",
		      sieve_script_label(script), userlog_notice);
		ret = 0;
		break;
	case SIEVE_EXEC_OK:
		ret = (estatus->keep_original ? 0 : 1);
		break;
	}

	return ret;
}

static int
imap_sieve_run_scripts(struct imap_sieve_run *isrun,
		       const struct sieve_message_data *msgdata,
		       const struct sieve_script_env *scriptenv, bool *fatal_r)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	struct imap_sieve_run_script *scripts = isrun->scripts;
	unsigned int count = isrun->scripts_count;
	struct sieve_resource_usage *rusage =
		&scriptenv->exec_status->resource_usage;
	struct sieve_multiscript *mscript;
	struct sieve_error_handler *ehandler;
	struct imap_sieve_run_script *last_script = NULL;
	bool more = TRUE, rusage_exceeded = FALSE;
	enum sieve_compile_flags cpflags;
	enum sieve_execute_flags exflags;
	enum sieve_error compile_error = SIEVE_ERROR_NONE;
	unsigned int i;
	int ret;

	*fatal_r = FALSE;

	/* Start execution */
	mscript = sieve_multiscript_start_execute(svinst, msgdata, scriptenv);

	/* Execute scripts */
	for (i = 0; i < count && more; i++) {
		struct sieve_script *script = scripts[i].script;
		struct sieve_binary *sbin = scripts[i].binary;
		bool user_script = scripts[i].user_script;
		int mstatus;

		cpflags = 0;
		exflags = SIEVE_EXECUTE_FLAG_NO_ENVELOPE |
			  SIEVE_EXECUTE_FLAG_SKIP_RESPONSES;

		last_script = &scripts[i];

		if (scripts[i].rusage_exceeded) {
			rusage_exceeded = TRUE;
			break;
		}

		sieve_resource_usage_init(rusage);
		if (user_script) {
			cpflags |= SIEVE_COMPILE_FLAG_NOGLOBAL;
			exflags |= SIEVE_EXECUTE_FLAG_NOGLOBAL;
			ehandler = isrun->user_ehandler;
		} else {
			cpflags |= SIEVE_COMPILE_FLAG_NO_ENVELOPE;
			ehandler = isieve->master_ehandler;
		}

		/* Open */
		if (sbin == NULL) {
			e_debug(sieve_get_event(svinst),
				"Opening script %d of %d from '%s'",
				i+1, count, sieve_script_label(script));

			/* Already known to fail */
			if (scripts[i].compile_error != SIEVE_ERROR_NONE) {
				compile_error = scripts[i].compile_error;
				break;
			}

			/* Try to open/compile binary */
			scripts[i].binary = sbin = imap_sieve_run_open_script(
				isrun, &scripts[i], cpflags, FALSE,
				&compile_error);
			if (sbin == NULL) {
				scripts[i].compile_error = compile_error;
				break;
			}
		}

		/* Execute */
		e_debug(sieve_get_event(svinst),
			"Executing script from '%s'",
			sieve_get_source(sbin));
		more = sieve_multiscript_run(mscript, sbin, ehandler, ehandler,
					     exflags);

		mstatus = sieve_multiscript_status(mscript);
		if (!more && mstatus == SIEVE_EXEC_BIN_CORRUPT &&
		    !scripts[i].binary_corrupt && sieve_is_loaded(sbin)) {
			/* Close corrupt script */
			sieve_close(&sbin);

			/* Recompile */
			scripts[i].binary = sbin =
				imap_sieve_run_open_script(
					isrun, &scripts[i], cpflags, FALSE,
					&compile_error);
			if (sbin == NULL) {
				scripts[i].compile_error = compile_error;
				break;
			}

			/* Execute again */
			more = sieve_multiscript_run(mscript, sbin,
						     ehandler, ehandler,
						     exflags);

			/* Save new version */

			mstatus = sieve_multiscript_status(mscript);
			if (mstatus == SIEVE_EXEC_BIN_CORRUPT)
				scripts[i].binary_corrupt = TRUE;
			else if (more)
				(void)sieve_save(sbin, FALSE, NULL);
		}

		if (user_script && !sieve_record_resource_usage(sbin, rusage)) {
			rusage_exceeded = ((i + 1) < count && more);
			scripts[i].rusage_exceeded = TRUE;
			break;
		}
	}

	/* Finish execution */
	exflags = SIEVE_EXECUTE_FLAG_NO_ENVELOPE |
		  SIEVE_EXECUTE_FLAG_SKIP_RESPONSES;
	ehandler = (isrun->user_ehandler != NULL ?
		    isrun->user_ehandler : isieve->master_ehandler);
	if (compile_error == SIEVE_ERROR_TEMP_FAILURE) {
		ret = sieve_multiscript_finish(&mscript, ehandler, exflags,
					       SIEVE_EXEC_TEMP_FAILURE);
	} else if (rusage_exceeded) {
		i_assert(last_script != NULL);
		(void)sieve_multiscript_finish(&mscript, ehandler, exflags,
					       SIEVE_EXEC_TEMP_FAILURE);
		sieve_error(ehandler, sieve_script_name(last_script->script),
			    "cumulative resource usage limit exceeded");
		ret = SIEVE_EXEC_RESOURCE_LIMIT;
	} else {
		ret = sieve_multiscript_finish(&mscript, ehandler, exflags,
					       SIEVE_EXEC_OK);
	}

	/* Don't log additional messages about compile failure */
	if (compile_error != SIEVE_ERROR_NONE && ret == SIEVE_EXEC_FAILURE) {
		e_info(sieve_get_event(svinst),
		       "Aborted script execution sequence "
		       "with successful implicit keep");
		return 1;
	}

	if (last_script == NULL && ret == SIEVE_EXEC_OK)
		return 0;
	return imap_sieve_handle_exec_status(isrun, last_script, ret,
					     scriptenv->exec_status, fatal_r);
}

int imap_sieve_run_mail(struct imap_sieve_run *isrun, struct mail *mail,
			const char *changed_flags, bool *fatal_r)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	struct mail_user *user = isieve->client->user;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	struct imap_sieve_context context;
	struct sieve_trace_config trace_config;
	struct sieve_trace_log *trace_log;
	const char *error;
	int ret;

	*fatal_r = FALSE;

	i_zero(&context);
	context.event.dest_mailbox = isrun->dest_mailbox;
	context.event.src_mailbox = isrun->src_mailbox;
	context.event.cause = isrun->cause;
	context.event.changed_flags = changed_flags;
	context.mail = mail;
	context.isieve = isieve;

	/* Initialize trace logging */
	imap_sieve_run_init_trace_log(isrun, &trace_config, &trace_log);

	T_BEGIN {
		if (trace_log != NULL) {
			/* Write trace header for message */
			sieve_trace_log_printf(trace_log,
				"Filtering message:\n"
				"\n"
				"  UID: %u\n", mail->uid);
			if (changed_flags != NULL && *changed_flags != '\0') {
				sieve_trace_log_printf(trace_log,
					"  Changed flags: %s\n", changed_flags);
			}
		}

		/* Collect necessary message data */

		i_zero(&msgdata);
		msgdata.mail = mail;
		msgdata.auth_user = user->username;
		(void)mail_get_message_id(msgdata.mail, &msgdata.id);

		/* Compose script execution environment */

		if (sieve_script_env_init(&scriptenv, user, &error) < 0) {
			e_error(sieve_get_event(svinst),
				"Failed to initialize script execution: %s",
				error);
			ret = -1;
		} else {
			scriptenv.default_mailbox =
				mailbox_get_vname(mail->box);
			scriptenv.smtp_start = imap_sieve_smtp_start;
			scriptenv.smtp_add_rcpt = imap_sieve_smtp_add_rcpt;
			scriptenv.smtp_send = imap_sieve_smtp_send;
			scriptenv.smtp_abort = imap_sieve_smtp_abort;
			scriptenv.smtp_finish = imap_sieve_smtp_finish;
			scriptenv.duplicate_transaction_begin =
				imap_sieve_duplicate_transaction_begin;
			scriptenv.duplicate_transaction_commit =
				imap_sieve_duplicate_transaction_commit;
			scriptenv.duplicate_transaction_rollback =
				imap_sieve_duplicate_transaction_rollback;
			scriptenv.duplicate_mark = imap_sieve_duplicate_mark;
			scriptenv.duplicate_check = imap_sieve_duplicate_check;
			scriptenv.result_amend_log_message =
				imap_sieve_result_amend_log_message;
			scriptenv.trace_log = trace_log;
			scriptenv.trace_config = trace_config;
			scriptenv.script_context = &context;

			i_zero(&estatus);
			scriptenv.exec_status = &estatus;

			/* Execute script(s) */

			ret = imap_sieve_run_scripts(isrun, &msgdata,
						     &scriptenv, fatal_r);
		}
	} T_END;

	return ret;
}
