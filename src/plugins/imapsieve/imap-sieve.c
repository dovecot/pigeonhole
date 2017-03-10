/* Copyright (c) 2016-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "home-expand.h"
#include "mail-storage.h"
#include "mail-user.h"
#include "lda-settings.h"
#include "mail-deliver.h"
#include "duplicate.h"
#include "smtp-client.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "ext-imapsieve-common.h"

#include "imap-sieve.h"

/*
 * Configuration
 */

#define IMAP_SIEVE_MAX_USER_ERRORS 30

/*
 * IMAP Sieve
 */

struct imap_sieve {
	pool_t pool;
	struct mail_user *user;
	const struct lda_settings *lda_set;
	const char *home_dir;

	struct sieve_instance *svinst;
	struct sieve_storage *storage;

	const struct sieve_extension *ext_imapsieve;
	const struct sieve_extension *ext_vnd_imapsieve;

	struct duplicate_context *dup_ctx;

	struct sieve_error_handler *master_ehandler;
};

static const char *
mail_sieve_get_setting(void *context, const char *identifier)
{
	struct imap_sieve *isieve = (struct imap_sieve *)context;

	return mail_user_plugin_getenv(isieve->user, identifier);
}

static const struct sieve_callbacks mail_sieve_callbacks = {
	NULL,
	mail_sieve_get_setting
};


struct imap_sieve *imap_sieve_init(struct mail_user *user,
	const struct lda_settings *lda_set)
{
	struct sieve_environment svenv;
	struct imap_sieve *isieve;
	bool debug = user->mail_debug;
	pool_t pool;

	pool = pool_alloconly_create("imap_sieve", 256);
	isieve = p_new(pool, struct imap_sieve, 1);
	isieve->pool = pool;
	isieve->user = user;
	isieve->lda_set = lda_set;

	isieve->dup_ctx = duplicate_init(user);

	i_zero(&svenv);
	svenv.username = user->username;
	(void)mail_user_get_home(user, &svenv.home_dir);
	svenv.hostname = lda_set->hostname;
	svenv.base_dir = user->set->base_dir;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;
	svenv.location = SIEVE_ENV_LOCATION_MS;
	svenv.delivery_phase = SIEVE_DELIVERY_PHASE_POST;

	isieve->home_dir = p_strdup(pool, svenv.home_dir);

	isieve->svinst = sieve_init
		(&svenv, &mail_sieve_callbacks, isieve, debug);

	isieve->ext_imapsieve = sieve_extension_replace
		(isieve->svinst, &imapsieve_extension, TRUE);
	isieve->ext_vnd_imapsieve = sieve_extension_replace
		(isieve->svinst, &vnd_imapsieve_extension, TRUE);

	isieve->master_ehandler = sieve_master_ehandler_create
		(isieve->svinst, NULL, 0); // FIXME: prefix?
	sieve_system_ehandler_set(isieve->master_ehandler);
	sieve_error_handler_accept_infolog(isieve->master_ehandler, TRUE);
	sieve_error_handler_accept_debuglog(isieve->master_ehandler, debug);

	return isieve;
}

void imap_sieve_deinit(struct imap_sieve **_isieve)
{
	struct imap_sieve *isieve = *_isieve;

	*_isieve = NULL;

	sieve_error_handler_unref(&isieve->master_ehandler);

	if (isieve->storage != NULL)
		sieve_storage_unref(&isieve->storage);
	sieve_extension_unregister(isieve->ext_imapsieve);
	sieve_extension_unregister(isieve->ext_vnd_imapsieve);
	sieve_deinit(&isieve->svinst);

	duplicate_deinit(&isieve->dup_ctx);

	pool_unref(&isieve->pool);
}

static int
imap_sieve_get_storage(struct imap_sieve *isieve,
	struct sieve_storage **storage_r)
{
	enum sieve_storage_flags storage_flags = 0;
	enum sieve_error error;

	if (isieve->storage != NULL) {
		*storage_r = isieve->storage;
		return 1;
	}

	// FIXME: limit interval between retries

	isieve->storage = sieve_storage_create_main
		(isieve->svinst, isieve->user, storage_flags, &error);
	if (isieve->storage == NULL) {
		if (error == SIEVE_ERROR_TEMP_FAILURE)
			return -1;
		return 0;
	}
	*storage_r = isieve->storage;
	return 1;
}

/*
 * Mail transmission
 */

static void *imap_sieve_smtp_start
(const struct sieve_script_env *senv, const char *return_path)
{
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;

	return (void *)smtp_client_init
		(isctx->isieve->lda_set, return_path);
}

static void imap_sieve_smtp_add_rcpt
(const struct sieve_script_env *senv ATTR_UNUSED, void *handle,
	const char *address)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	smtp_client_add_rcpt(smtp_client, address);
}

static struct ostream *imap_sieve_smtp_send
(const struct sieve_script_env *senv ATTR_UNUSED, void *handle)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	return smtp_client_send(smtp_client);
}

static void imap_sieve_smtp_abort
(const struct sieve_script_env *senv ATTR_UNUSED, void *handle)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	smtp_client_abort(&smtp_client);
}

static int imap_sieve_smtp_finish
(const struct sieve_script_env *senv ATTR_UNUSED, void *handle,
	const char **error_r)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	return smtp_client_deinit_timeout
		(smtp_client, LDA_SUBMISSION_TIMEOUT_SECS, error_r);
}

/*
 * Duplicate checking
 */

static bool imap_sieve_duplicate_check
(const struct sieve_script_env *senv, const void *id,
	size_t id_size)
{
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;

	return duplicate_check(isctx->isieve->dup_ctx,
		id, id_size, senv->user->username);
}

static void imap_sieve_duplicate_mark
(const struct sieve_script_env *senv, const void *id,
	size_t id_size, time_t time)
{
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;

	duplicate_mark(isctx->isieve->dup_ctx,
		id, id_size, senv->user->username, time);
}

static void imap_sieve_duplicate_flush
(const struct sieve_script_env *senv)
{
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;
	duplicate_flush(isctx->isieve->dup_ctx);
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

	/* Binary corrupt after recompile; don't recompile again */
	unsigned int binary_corrupt:1;
};

struct imap_sieve_run {
	pool_t pool;
	struct imap_sieve *isieve;
	struct mailbox *dest_mailbox, *src_mailbox;
	char *cause;

	struct sieve_error_handler *user_ehandler;
	char *userlog;

	struct sieve_script *user_script;
	struct imap_sieve_run_script *scripts;
	unsigned int scripts_count;
};

static void
imap_sieve_run_init_user_log(struct imap_sieve_run *isrun)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	const char *log_path;

	log_path = sieve_user_get_log_path
		(svinst, isrun->user_script);
	if ( log_path != NULL ) {
		isrun->userlog = p_strdup(isrun->pool, log_path);
		isrun->user_ehandler = sieve_logfile_ehandler_create
			(svinst, log_path, IMAP_SIEVE_MAX_USER_ERRORS);
	}
}

int imap_sieve_run_init(struct imap_sieve *isieve,
	struct mailbox *dest_mailbox, struct mailbox *src_mailbox,
	const char *cause, const char *script_name,
	const char *const *scripts_before,
	const char *const *scripts_after,
	struct imap_sieve_run **isrun_r)
{
	struct sieve_instance *svinst = isieve->svinst;
	struct imap_sieve_run *isrun;
	struct sieve_storage *storage;
	struct imap_sieve_run_script *scripts;
	struct sieve_script *user_script;
	const char *const *sp;
	enum sieve_error error;
	pool_t pool;
	unsigned int max_len, count;
	int ret;

	/* Determine how many scripts we may run for this event */
	max_len = 0;
	if (scripts_before != NULL)
		max_len += str_array_length(scripts_before);
	if (script_name != NULL)
		max_len++;
	if (scripts_after != NULL)
		max_len += str_array_length(scripts_after);
	if (max_len == 0)
		return 0;

	/* Get storage for user script */
	storage = NULL;
	if (script_name != NULL && *script_name != '\0' &&
		(ret=imap_sieve_get_storage(isieve, &storage)) < 0)
		return ret;

	/* Open all scripts */
	count = 0;
	pool = pool_alloconly_create("imap_sieve_run", 256);
	scripts = p_new(pool, struct imap_sieve_run_script, max_len);

	/* Admin scripts before user script */
	if (scripts_before != NULL) {
		for (sp = scripts_before; *sp != NULL; sp++) {
			i_assert(count < max_len);
			scripts[count].script = sieve_script_create_open
				(svinst, *sp, NULL, &error);
			if (scripts[count].script != NULL)
				count++;
			else if (error == SIEVE_ERROR_TEMP_FAILURE)
				return -1;
		}
	}

	/* The user script */
	user_script = NULL;
	if (storage != NULL) {
		i_assert(count < max_len);
		scripts[count].script = sieve_storage_open_script
			(storage, script_name, &error);
		if (scripts[count].script != NULL) {
			user_script = scripts[count].script;
			count++;
		} else if (error == SIEVE_ERROR_TEMP_FAILURE) {
			return -1;
		}
	}

	/* Admin scripts after user script */
	if (scripts_after != NULL) {
		for (sp = scripts_after; *sp != NULL; sp++) {
			i_assert(count < max_len);
			scripts[count].script = sieve_script_create_open
				(svinst, *sp, NULL, &error);
			if (scripts[count].script != NULL)
				count++;
			else if (error == SIEVE_ERROR_TEMP_FAILURE)
				return -1;
		}
	}

	if (count == 0) {
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
	isrun->user_script = user_script;
	isrun->scripts = scripts;
	isrun->scripts_count = count;

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
		if (isrun->scripts[i].script != NULL)
			sieve_script_unref(&isrun->scripts[i].script);
	}
	if (isrun->user_ehandler != NULL)
		sieve_error_handler_unref(&isrun->user_ehandler);

	pool_unref(&isrun->pool);
}

static struct sieve_binary *
imap_sieve_run_open_script(
	struct imap_sieve_run *isrun,
	struct sieve_script *script,
	enum sieve_compile_flags cpflags,
	bool recompile, enum sieve_error *error_r)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	const char *compile_name = "compile";
	bool debug = isieve->user->mail_debug;

	if ( recompile ) {
		/* Warn */
		sieve_sys_warning(svinst,
			"Encountered corrupt binary: re-compiling script %s",
			sieve_script_location(script));
		compile_name = "re-compile";
	} else 	if ( debug ) {
		sieve_sys_debug(svinst,
			"Loading script %s", sieve_script_location(script));
	}

	if ( script == isrun->user_script )
		ehandler = isrun->user_ehandler;
	else
		ehandler = isieve->master_ehandler;
	sieve_error_handler_reset(ehandler);

	/* Load or compile the sieve script */
	if ( recompile ) {
		sbin = sieve_compile_script
			(script, ehandler, cpflags, error_r);
	} else {
		sbin = sieve_open_script
			(script, ehandler, cpflags, error_r);
	}

	/* Handle error */
	if ( sbin == NULL ) {
		switch ( *error_r ) {
		/* Script not found */
		case SIEVE_ERROR_NOT_FOUND:
			if ( debug ) {
				sieve_sys_debug(svinst, "Script `%s' is missing for %s",
					sieve_script_location(script), compile_name);
			}
			break;
		/* Temporary failure */
		case SIEVE_ERROR_TEMP_FAILURE:
			sieve_sys_error(svinst,
				"Failed to open script `%s' for %s (temporary failure)",
				sieve_script_location(script), compile_name);
			break;
		/* Compile failed */
		case SIEVE_ERROR_NOT_VALID:
			if (script == isrun->user_script && isrun->userlog != NULL ) {
				sieve_sys_info(svinst,
					"Failed to %s script `%s' "
					"(view user logfile `%s' for more information)",
					compile_name, sieve_script_location(script),
					isrun->userlog);
				break;
			}
			sieve_sys_error(svinst,	"Failed to %s script `%s'",
				compile_name, sieve_script_location(script));
			break;
		/* Something else */
		default:
			sieve_sys_error(svinst,	"Failed to open script `%s' for %s",
				sieve_script_location(script), compile_name);
			break;
		}

		return NULL;
	}

	if (!recompile)
		(void)sieve_save(sbin, FALSE, NULL);
	return sbin;
}

static int imap_sieve_handle_exec_status
(struct imap_sieve_run *isrun,
	struct sieve_script *script, int status, bool keep,
	struct sieve_exec_status *estatus)
	ATTR_NULL(2)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	const char *userlog_notice = "";
	sieve_sys_error_func_t error_func, user_error_func;
	enum mail_error mail_error = MAIL_ERROR_NONE;
	int ret = -1;

	error_func = user_error_func = sieve_sys_error;

	if ( estatus != NULL && estatus->last_storage != NULL &&
		estatus->store_failed) {
		mail_storage_get_last_error(estatus->last_storage, &mail_error);

		/* Don't bother administrator too much with benign errors */
		if ( mail_error == MAIL_ERROR_NOQUOTA ) {
			error_func = sieve_sys_info;
			user_error_func = sieve_sys_info;
		}
	}

	if ( script == isrun->user_script && isrun->userlog != NULL ) {
		userlog_notice = t_strdup_printf
			(" (user logfile %s may reveal additional details)",
				isrun->userlog);
		user_error_func = sieve_sys_info;
	}

	switch ( status ) {
	case SIEVE_EXEC_FAILURE:
		user_error_func(svinst,
			"Execution of script %s failed%s",
			sieve_script_location(script), userlog_notice);
		ret = 0;
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
		error_func(svinst,
			"Execution of script %s was aborted "
			"due to temporary failure%s",
			sieve_script_location(script), userlog_notice);
		ret = -1;
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
		sieve_sys_error(svinst,
			"!!BUG!!: Binary compiled from %s is still corrupt; "
			"bailing out and reverting to default action",
			sieve_script_location(script));
		ret = 0;
		break;
	case SIEVE_EXEC_KEEP_FAILED:
		error_func(svinst,
			"Execution of script %s failed "
			"with unsuccessful implicit keep%s",
			sieve_script_location(script), userlog_notice);
		ret = 0;
		break;
	case SIEVE_EXEC_OK:
		ret = (keep ? 0 : 1);
		break;
	}

	return ret;
}

static int imap_sieve_run_scripts
(struct imap_sieve_run *isrun,
	const struct sieve_message_data *msgdata,
	const struct sieve_script_env *scriptenv)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	struct imap_sieve_run_script *scripts = isrun->scripts;
	unsigned int count = isrun->scripts_count;
	struct sieve_multiscript *mscript;
	struct sieve_error_handler *ehandler;
	struct sieve_script *last_script = NULL;
	bool user_script = FALSE, more = TRUE;
	bool debug = isieve->user->mail_debug, keep = TRUE;
	enum sieve_compile_flags cpflags;
	enum sieve_execute_flags exflags;
	enum sieve_error compile_error = SIEVE_ERROR_NONE;
	unsigned int i;
	int ret;

	/* Start execution */
	mscript = sieve_multiscript_start_execute
		(svinst, msgdata, scriptenv);

	/* Execute scripts */
	for ( i = 0; i < count && more; i++ ) {
		struct sieve_script *script = scripts[i].script;
		struct sieve_binary *sbin = scripts[i].binary;

		cpflags = 0;
		exflags = SIEVE_EXECUTE_FLAG_DEFER_KEEP |
			SIEVE_EXECUTE_FLAG_NO_ENVELOPE;

		user_script = ( script == isrun->user_script );
		last_script = script;

		if ( user_script ) {
			cpflags |= SIEVE_COMPILE_FLAG_NOGLOBAL;
			exflags |= SIEVE_EXECUTE_FLAG_NOGLOBAL;
			ehandler = isrun->user_ehandler;
		} else {
			cpflags |= SIEVE_COMPILE_FLAG_NO_ENVELOPE;
			ehandler = isieve->master_ehandler;
		}

		/* Open */
		if (sbin == NULL) {
			if ( debug ) {
				sieve_sys_debug(svinst,
					"Opening script %d of %d from `%s'",
					i+1, count, sieve_script_location(script));
			}

			/* Already known to fail */
			if (scripts[i].compile_error != SIEVE_ERROR_NONE) {
				compile_error = scripts[i].compile_error;
				break;
			}

			/* Try to open/compile binary */
			scripts[i].binary = sbin = imap_sieve_run_open_script
				(isrun, script, cpflags, FALSE, &compile_error);
			if ( sbin == NULL ) {
				scripts[i].compile_error = compile_error;
				break;
			}
		}

		/* Execute */
		if ( debug ) {
			sieve_sys_debug(svinst,
				"Executing script from `%s'",
				sieve_get_source(sbin));
		}
		more = sieve_multiscript_run(mscript,
			sbin, ehandler, ehandler, exflags);

		if ( !more ) {
			if ( !scripts[i].binary_corrupt &&
				sieve_multiscript_status(mscript)
					== SIEVE_EXEC_BIN_CORRUPT &&
				sieve_is_loaded(sbin) ) {

				/* Close corrupt script */
				sieve_close(&sbin);

				/* Recompile */
				scripts[i].binary = sbin = imap_sieve_run_open_script
					(isrun, script, cpflags, FALSE, &compile_error);
				if ( sbin == NULL ) {
					scripts[i].compile_error = compile_error;
					break;
				}

				/* Execute again */
				more = sieve_multiscript_run(mscript, sbin,
					ehandler, ehandler, exflags);

				/* Save new version */

				if ( sieve_multiscript_status(mscript)
					== SIEVE_EXEC_BIN_CORRUPT )
					scripts[i].binary_corrupt = TRUE;
				else if ( more )
					(void)sieve_save(sbin, FALSE, NULL);
			}
		}
	}

	/* Finish execution */
	exflags = SIEVE_EXECUTE_FLAG_DEFER_KEEP |
		SIEVE_EXECUTE_FLAG_NO_ENVELOPE;
	ehandler = (isrun->user_ehandler != NULL ?
		isrun->user_ehandler : isieve->master_ehandler);
	if ( compile_error == SIEVE_ERROR_TEMP_FAILURE ) {
		ret = sieve_multiscript_tempfail
			(&mscript, ehandler, exflags);
	} else {
		ret = sieve_multiscript_finish
			(&mscript, ehandler, exflags, &keep);
	}

	/* Don't log additional messages about compile failure */
	if ( compile_error != SIEVE_ERROR_NONE &&
		ret == SIEVE_EXEC_FAILURE ) {
		sieve_sys_info(svinst,
			"Aborted script execution sequence "
			"with successful implicit keep");
		return 1;
	}

	return imap_sieve_handle_exec_status
		(isrun, last_script, ret, keep, scriptenv->exec_status);
}

int imap_sieve_run_mail
(struct imap_sieve_run *isrun, struct mail *mail,
	const char *changed_flags)
{
	struct imap_sieve *isieve = isrun->isieve;
	struct sieve_instance *svinst = isieve->svinst;
	const struct lda_settings *lda_set = isieve->lda_set;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	struct imap_sieve_context context;
	struct sieve_trace_config trace_config;
	struct sieve_trace_log *trace_log;
	int ret;

	i_zero(&context);
	context.event.dest_mailbox = isrun->dest_mailbox;
	context.event.src_mailbox = isrun->src_mailbox;
	context.event.cause = isrun->cause;
	context.event.changed_flags = changed_flags;
	context.isieve = isieve;

	/* Initialize trace logging */

	trace_log = NULL;
	if ( sieve_trace_config_get(svinst, &trace_config) >= 0) {
		const char *tr_label = t_strdup_printf
			("%s.%s.%u", isieve->user->username,
				mailbox_get_vname(mail->box), mail->uid);
		if ( sieve_trace_log_open(svinst, tr_label, &trace_log) < 0 )
			i_zero(&trace_config);
	}

	T_BEGIN {
		/* Collect necessary message data */

		i_zero(&msgdata);
		msgdata.mail = mail;
		msgdata.auth_user = isieve->user->username;
		(void)mail_get_first_header
			(msgdata.mail, "Message-ID", &msgdata.id);

		/* Compose script execution environment */

		i_zero(&scriptenv);
		i_zero(&estatus);
		scriptenv.default_mailbox = mailbox_get_vname(mail->box);
		scriptenv.user = isieve->user;
		scriptenv.postmaster_address = lda_set->postmaster_address;
		scriptenv.smtp_start = imap_sieve_smtp_start;
		scriptenv.smtp_add_rcpt = imap_sieve_smtp_add_rcpt;
		scriptenv.smtp_send = imap_sieve_smtp_send;
		scriptenv.smtp_abort = imap_sieve_smtp_abort;
		scriptenv.smtp_finish = imap_sieve_smtp_finish;
		scriptenv.duplicate_mark = imap_sieve_duplicate_mark;
		scriptenv.duplicate_check = imap_sieve_duplicate_check;
		scriptenv.duplicate_flush = imap_sieve_duplicate_flush;
		scriptenv.exec_status = &estatus;
		scriptenv.trace_log = trace_log;
		scriptenv.trace_config = trace_config;
		scriptenv.script_context = (void *)&context;

		/* Execute script(s) */

		ret = imap_sieve_run_scripts(isrun, &msgdata, &scriptenv);
	} T_END;

	if ( trace_log != NULL )
		sieve_trace_log_free(&trace_log);

	return ret;
}
