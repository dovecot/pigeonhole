/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "array.h"
#include "home-expand.h"
#include "var-expand.h"
#include "eacces-error.h"
#include "smtp-address.h"
#include "smtp-submit.h"
#include "mail-storage.h"
#include "mail-deliver.h"
#include "mail-user.h"
#include "mail-duplicate.h"
#include "smtp-submit.h"
#include "mail-send.h"
#include "lda-settings.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "lda-sieve-plugin.h"

#include <sys/stat.h>
#include <dirent.h>

/*
 * Configuration
 */

#define LDA_SIEVE_DEFAULT_LOCATION "~/.dovecot.sieve"

#define LDA_SIEVE_MAX_USER_ERRORS 30

/*
 * Global variables
 */

static deliver_mail_func_t *next_deliver_mail;

/*
 * Settings handling
 */

static const char *
lda_sieve_get_setting(void *context, const char *identifier)
{
	struct mail_deliver_context *mdctx =
		(struct mail_deliver_context *)context;
	const char *value = NULL;

	if (mdctx == NULL)
		return NULL;

	if (mdctx->rcpt_user == NULL ||
	    (value = mail_user_plugin_getenv(
		mdctx->rcpt_user, identifier)) == NULL) {
		if (strcmp(identifier, "recipient_delimiter") == 0)
			value = mdctx->set->recipient_delimiter;
	}

	return value;
}

static const struct sieve_callbacks lda_sieve_callbacks = {
	NULL,
	lda_sieve_get_setting
};

/*
 * Mail transmission
 */

static void *
lda_sieve_smtp_start(const struct sieve_script_env *senv,
		     const struct smtp_address *mail_from)
{
	struct mail_deliver_context *dctx =
		(struct mail_deliver_context *)senv->script_context;
	struct smtp_submit_input submit_input;

	i_zero(&submit_input);

	return smtp_submit_init_simple(&submit_input, dctx->smtp_set,
				       mail_from);
}

static void
lda_sieve_smtp_add_rcpt(const struct sieve_script_env *senv ATTR_UNUSED,
			void *handle, const struct smtp_address *rcpt_to)
{
	struct smtp_submit *smtp_submit = (struct smtp_submit *) handle;

	smtp_submit_add_rcpt(smtp_submit, rcpt_to);
}

static struct ostream *
lda_sieve_smtp_send(const struct sieve_script_env *senv ATTR_UNUSED,
		    void *handle)
{
	struct smtp_submit *smtp_submit = (struct smtp_submit *) handle;

	return smtp_submit_send(smtp_submit);
}

static void
lda_sieve_smtp_abort(const struct sieve_script_env *senv ATTR_UNUSED,
		     void *handle)
{
	struct smtp_submit *smtp_submit = (struct smtp_submit *) handle;

	smtp_submit_deinit(&smtp_submit);
}

static int
lda_sieve_smtp_finish(const struct sieve_script_env *senv ATTR_UNUSED,
		      void *handle, const char **error_code_r)
{
	struct smtp_submit *smtp_submit = (struct smtp_submit *) handle;
	int ret;

	ret = smtp_submit_run(smtp_submit, error_code_r);
	smtp_submit_deinit(&smtp_submit);
	return ret;
}

static int
lda_sieve_reject_mail(const struct sieve_script_env *senv,
		      const struct smtp_address *recipient,
		      const char *reason)
{
	struct mail_deliver_context *dctx =
		(struct mail_deliver_context *)senv->script_context;

	return mail_send_rejection(dctx, recipient, reason);
}

/*
 * Duplicate checking
 */

static void *
lda_sieve_duplicate_transaction_begin(const struct sieve_script_env *senv)
{
	struct mail_deliver_context *dctx =
		(struct mail_deliver_context *)senv->script_context;

	return mail_duplicate_transaction_begin(dctx->dup_db);
}

static void lda_sieve_duplicate_transaction_commit(void **_dup_trans)
{
	struct mail_duplicate_transaction *dup_trans = *_dup_trans;

	*_dup_trans = NULL;
	mail_duplicate_transaction_commit(&dup_trans);
}

static void lda_sieve_duplicate_transaction_rollback(void **_dup_trans)
{
	struct mail_duplicate_transaction *dup_trans = *_dup_trans;

	*_dup_trans = NULL;
	mail_duplicate_transaction_rollback(&dup_trans);
}

static enum sieve_duplicate_check_result
lda_sieve_duplicate_check(void *_dup_trans, const struct sieve_script_env *senv,
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
lda_sieve_duplicate_mark(void *_dup_trans, const struct sieve_script_env *senv,
			 const void *id, size_t id_size, time_t time)
{
	struct mail_duplicate_transaction *dup_trans = _dup_trans;

	mail_duplicate_mark(dup_trans, id, id_size, senv->user->username, time);
}

/*
 * Result logging
 */

static const char *
lda_sieve_result_amend_log_message(const struct sieve_script_env *senv,
				   enum log_type log_type ATTR_UNUSED,
				   const char *message)
{
	struct mail_deliver_context *mdctx = senv->script_context;
	string_t *str;
	const char *error;

	const struct var_expand_params params = {
		.table = mail_deliver_ctx_get_log_var_expand_table(mdctx, message),
	};

	str = t_str_new(256);
	if (var_expand(str, mdctx->set->deliver_log_format, &params,
		       &error) < 0) {
		e_error(mdctx->event,
			"Failed to expand deliver_log_format=%s: %s",
			mdctx->set->deliver_log_format, error);
	}
	return str_c(str);
}

/*
 * Plugin implementation
 */

struct lda_sieve_run_context {
	struct sieve_instance *svinst;

	struct mail_deliver_context *mdctx;
	const char *home_dir;

	struct sieve_script **scripts;
	unsigned int script_count;

	struct sieve_script *user_script;
	struct sieve_script *main_script;
	struct sieve_script *discard_script;

	const struct sieve_message_data *msgdata;
	const struct sieve_script_env *scriptenv;

	struct sieve_error_handler *user_ehandler;
	struct sieve_error_handler *master_ehandler;
	struct sieve_error_handler *action_ehandler;
	const char *userlog;
};

static int
lda_sieve_get_personal_storage(struct sieve_instance *svinst,
			       struct mail_user *user,
			       struct sieve_storage **storage_r,
			       enum sieve_error *error_code_r)
{
	if (sieve_storage_create_personal(svinst, user, 0,
					  storage_r, error_code_r) < 0) {
		switch (*error_code_r) {
		case SIEVE_ERROR_NOT_POSSIBLE:
		case SIEVE_ERROR_NOT_FOUND:
			break;
		case SIEVE_ERROR_TEMP_FAILURE:
			e_error(sieve_get_event(svinst),
				"Failed to access user's personal storage "
				"(temporary failure)");
			return -1;
		default:
			e_error(sieve_get_event(svinst),
				"Failed to access user's personal storage");
			break;
		}
		return 0;
	}
	return 1;
}

static void
lda_sieve_multiscript_log_error(struct event *event,
				const char *label, const char *location,
				enum sieve_error error_code)
{
	switch (error_code) {
	case SIEVE_ERROR_TEMP_FAILURE:
		e_error(event, "Failed to access %s script from '%s' "
			"(temporary failure)",
			label, location);
		break;
	default:
		break;
	}
}

static int
lda_sieve_multiscript_get_scripts(struct sieve_instance *svinst,
				  const char *label, const char *location,
				  ARRAY_TYPE(sieve_script) *scripts,
				  enum sieve_error *error_code_r)
{
	struct sieve_script_sequence *sseq;
	struct sieve_script *script;
	int ret;

	ret = sieve_script_sequence_create(svinst, location,
					   &sseq, error_code_r);
	if (ret < 0) {
		if (*error_code_r == SIEVE_ERROR_NOT_FOUND) {
			*error_code_r = SIEVE_ERROR_NONE;
			return 0;
		}
		lda_sieve_multiscript_log_error(svinst->event, label, location,
						*error_code_r);
		return -1;
	}

	while ((ret = sieve_script_sequence_next(sseq, &script,
						 error_code_r)) > 0)
		array_append(scripts, &script, 1);

	sieve_script_sequence_free(&sseq);
	if (ret < 0) {
		lda_sieve_multiscript_log_error(svinst->event, label, location,
						*error_code_r);
		return -1;
	}
	return 0;
}

static void
lda_sieve_binary_save(struct lda_sieve_run_context *srctx,
		      struct sieve_binary *sbin, struct sieve_script *script)
{
	enum sieve_error error_code;

	/* Save binary when compiled */
	if (sieve_save(sbin, FALSE, &error_code) < 0 &&
	    error_code == SIEVE_ERROR_NO_PERMISSION &&
	    script != srctx->user_script) {
		/* Cannot save binary for global script */
		e_error(sieve_get_event(srctx->svinst),
			"The LDA Sieve plugin does not have permission "
			"to save global Sieve script binaries; "
			"global Sieve scripts like '%s' need to be "
			"pre-compiled using the sievec tool",
			sieve_script_location(script));
	}
}

static struct
sieve_binary *lda_sieve_open(struct lda_sieve_run_context *srctx,
			     struct sieve_script *script,
			     enum sieve_compile_flags cpflags, bool recompile,
			     enum sieve_error *error_code_r)
{
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	const char *compile_name = "compile";
	int ret;

	if (recompile) {
		/* Warn */
		e_warning(sieve_get_event(svinst),
			  "Encountered corrupt binary: re-compiling script %s",
			  sieve_script_location(script));
		compile_name = "re-compile";
	} else {
		e_debug(sieve_get_event(svinst),
			"Loading script %s", sieve_script_location(script));
	}

	if (script == srctx->user_script)
		ehandler = srctx->user_ehandler;
	else
		ehandler = srctx->master_ehandler;

	sieve_error_handler_reset(ehandler);

	if (recompile) {
		ret = sieve_compile_script(script, ehandler, cpflags,
					   &sbin, error_code_r);
	} else {
		ret = sieve_open_script(script, ehandler, cpflags,
					&sbin, error_code_r);
	}

	/* Load or compile the sieve script */
	if (ret < 0) {
		switch (*error_code_r) {
		/* Script not found */
		case SIEVE_ERROR_NOT_FOUND:
			e_debug(sieve_get_event(svinst),
				"Script '%s' is missing for %s",
				sieve_script_location(script),
				compile_name);
			break;
		/* Temporary failure */
		case SIEVE_ERROR_TEMP_FAILURE:
			e_error(sieve_get_event(svinst),
				"Failed to open script '%s' for %s "
				"(temporary failure)",
				sieve_script_location(script), compile_name);
			break;
		/* Compile failed */
		case SIEVE_ERROR_NOT_VALID:
			if (script == srctx->user_script &&
			    srctx->userlog != NULL ) {
				e_info(sieve_get_event(svinst),
				       "Failed to %s script '%s' "
				       "(view user logfile '%s' for more information)",
				       compile_name,
				       sieve_script_location(script),
				       srctx->userlog);
				break;
			}
			e_error(sieve_get_event(svinst),
				"Failed to %s script '%s'",
				compile_name, sieve_script_location(script));
			break;
		/* Cumulative resource limit exceeded */
		case SIEVE_ERROR_RESOURCE_LIMIT:
			e_error(sieve_get_event(svinst),
				"Failed to open script '%s' for %s "
				"(cumulative resource limit exceeded)",
				sieve_script_location(script), compile_name);
			break;
		/* Something else */
		default:
			e_error(sieve_get_event(svinst),
				"Failed to open script '%s' for %s",
				sieve_script_location(script), compile_name);
			break;
		}

		return NULL;
	}

	if (!recompile)
		lda_sieve_binary_save(srctx, sbin, script);
	return sbin;
}

static int
lda_sieve_handle_exec_status(struct lda_sieve_run_context *srctx,
			     struct sieve_script *script, int status)
{
	struct sieve_instance *svinst = srctx->svinst;
	struct mail_deliver_context *mdctx = srctx->mdctx;
	struct sieve_exec_status *estatus = srctx->scriptenv->exec_status;
	const char *userlog_notice = "";
	enum log_type log_level, user_log_level;
	enum mail_error mail_error = MAIL_ERROR_NONE;
	int ret;

	log_level = user_log_level = LOG_TYPE_ERROR;

	if (estatus != NULL && estatus->last_storage != NULL &&
	    estatus->store_failed) {
		mail_storage_get_last_error(estatus->last_storage, &mail_error);

		/* Don't bother administrator too much with benign errors */
		if (mail_error == MAIL_ERROR_NOQUOTA) {
			log_level = LOG_TYPE_INFO;
			user_log_level = LOG_TYPE_INFO;
		}
	}

	if (script == srctx->user_script && srctx->userlog != NULL) {
		userlog_notice = t_strdup_printf(
			" (user logfile %s may reveal additional details)",
			srctx->userlog);
		user_log_level = LOG_TYPE_INFO;
	}

	switch (status) {
	case SIEVE_EXEC_FAILURE:
		e_log(sieve_get_event(svinst), user_log_level,
		      "Execution of script %s failed, "
		      "but implicit keep was successful%s",
		      sieve_script_location(script), userlog_notice);
		ret = 1;
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
		e_log(sieve_get_event(svinst), log_level,
		      "Execution of script %s was aborted due to temporary failure%s",
		      sieve_script_location(script), userlog_notice);
		if (mail_error != MAIL_ERROR_TEMP &&
		    mdctx->tempfail_error == NULL) {
			mdctx->tempfail_error =
				"Execution of Sieve filters was aborted due to temporary failure";
		}
		ret = -1;
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
		e_error(sieve_get_event(svinst),
			"!!BUG!!: Binary compiled from %s is still corrupt; "
			"bailing out and reverting to default delivery",
			sieve_script_location(script));
		ret = -1;
		break;
	case SIEVE_EXEC_RESOURCE_LIMIT:
		e_error(sieve_get_event(svinst),
			"Execution of script %s was aborted "
			"due to excessive resource usage",
			sieve_script_location(script));
		ret = -1;
		break;
	case SIEVE_EXEC_KEEP_FAILED:
		e_log(sieve_get_event(svinst), log_level,
		      "Execution of script %s failed with unsuccessful implicit keep%s",
		      sieve_script_location(script), userlog_notice);
		ret = -1;
		break;
	default:
		ret = status > 0 ? 1 : -1;
		break;
	}

	return ret;
}

static int
lda_sieve_execute_script(struct lda_sieve_run_context *srctx,
			 struct sieve_multiscript *mscript,
			 struct sieve_script *script,
			 unsigned int index, bool discard_script,
			 enum sieve_error *error_code_r)
{
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_error_handler *exec_ehandler;
	struct sieve_binary *sbin = NULL;
	enum sieve_compile_flags cpflags = 0;
	enum sieve_execute_flags exflags = SIEVE_EXECUTE_FLAG_LOG_RESULT;
	struct sieve_resource_usage *rusage =
		&srctx->scriptenv->exec_status->resource_usage;
	bool user_script;
	int mstatus, ret;

	*error_code_r = SIEVE_ERROR_NONE;

	user_script = (script == srctx->user_script);

	sieve_resource_usage_init(rusage);
	if (user_script) {
		cpflags |= SIEVE_COMPILE_FLAG_NOGLOBAL;
		exflags |= SIEVE_EXECUTE_FLAG_NOGLOBAL;
		exec_ehandler = srctx->user_ehandler;
	} else {
		exec_ehandler = srctx->master_ehandler;
	}

	/* Open */

	if (!discard_script) {
		e_debug(sieve_get_event(svinst),
			"Opening script %d of %d from '%s'",
			index, srctx->script_count,
			sieve_script_location(script));
	} else {
		e_debug(sieve_get_event(svinst),
			"Opening discard script from '%s'",
			sieve_script_location(script));
	}

	sbin = lda_sieve_open(srctx, script, cpflags, FALSE, error_code_r);
	if (sbin == NULL)
		return 0;

	/* Execute */

	e_debug(sieve_get_event(svinst),
		"Executing script from '%s'",
		sieve_get_source(sbin));

	if (!discard_script) {
		ret = (sieve_multiscript_run(mscript, sbin, exec_ehandler,
					     exec_ehandler, exflags) ? 1 : 0);
	} else {
		sieve_multiscript_run_discard(mscript, sbin, exec_ehandler,
					      exec_ehandler, exflags);
		ret = 0;
	}

	mstatus = sieve_multiscript_status(mscript);
	if (ret == 0 && mstatus == SIEVE_EXEC_BIN_CORRUPT &&
	    sieve_is_loaded(sbin)) {
		/* Close corrupt script */

		sieve_close(&sbin);

		/* Recompile */

		sbin = lda_sieve_open(srctx, script, cpflags, TRUE,
				      error_code_r);
		if (sbin == NULL)
			return 0;

		/* Execute again */

		if (!discard_script) {
			ret = (sieve_multiscript_run(
				mscript, sbin, exec_ehandler,
				exec_ehandler, exflags) ? 1 : 0);
		} else {
			sieve_multiscript_run_discard(
				mscript, sbin, exec_ehandler,
				exec_ehandler, exflags);
		}

		/* Save new version */

		mstatus = sieve_multiscript_status(mscript);
		if (mstatus != SIEVE_EXEC_BIN_CORRUPT)
			lda_sieve_binary_save(srctx, sbin, script);
	}
	if (ret == 0 && mstatus == SIEVE_EXEC_RESOURCE_LIMIT)
		ret = -1;

	if (user_script)
		(void)sieve_record_resource_usage(sbin, rusage);

	sieve_close(&sbin);

	return ret;
}

static int lda_sieve_execute_scripts(struct lda_sieve_run_context *srctx)
{
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_multiscript *mscript;
	struct sieve_error_handler *exec_ehandler;
	struct sieve_script *script, *last_script = NULL;
	enum sieve_execute_flags exflags = SIEVE_EXECUTE_FLAG_LOG_RESULT;
	bool discard_script;
	enum sieve_error error_code;
	unsigned int i;
	int ret;

	i_assert(srctx->script_count > 0);

	/* Start execution */

	mscript = sieve_multiscript_start_execute(svinst, srctx->msgdata,
						  srctx->scriptenv);

	/* Execute scripts */

	i = 0;
	discard_script = FALSE;
	error_code = SIEVE_ERROR_NONE;
	for (;;) {
		if (!discard_script) {
			/* normal script sequence */
			i_assert(i < srctx->script_count);
			script = srctx->scripts[i];
			i++;
		} else {
			/* discard script */
			script = srctx->discard_script;
		}

		i_assert(script != NULL);
		last_script = script;

		ret = lda_sieve_execute_script(srctx, mscript, script, i,
					       discard_script, &error_code);
		if (ret < 0)
			break;
		if (error_code == SIEVE_ERROR_NOT_FOUND) {
			/* skip scripts which finally turn out not to exist */
			ret = 1;
		}

		if (discard_script) {
			/* Executed discard script, which is always final */
			break;
		} else if (ret > 0) {
			/* The "keep" action is applied; execute next script */
			i_assert(i <= srctx->script_count);
			if (i == srctx->script_count) {
				/* End of normal script sequence */
				break;
			}
		} else if (error_code != SIEVE_ERROR_NONE) {
			break;
		} else if (sieve_multiscript_will_discard(mscript) &&
			   srctx->discard_script != NULL) {
			/* Mail is set to be discarded, but we have a discard script. */
			discard_script = TRUE;
		} else {
			break;
		}
	}

	/* Finish execution */
	exec_ehandler = (srctx->user_ehandler != NULL ?
			 srctx->user_ehandler : srctx->master_ehandler);
	ret = sieve_multiscript_finish(&mscript, exec_ehandler, exflags,
				       (error_code == SIEVE_ERROR_TEMP_FAILURE ?
					SIEVE_EXEC_TEMP_FAILURE :
					SIEVE_EXEC_OK));

	/* Don't log additional messages about compile failure */
	if (error_code != SIEVE_ERROR_NONE && ret == SIEVE_EXEC_FAILURE) {
		e_info(sieve_get_event(svinst),
		       "Aborted script execution sequence with successful implicit keep");
		return 1;
	}

	return lda_sieve_handle_exec_status(srctx, last_script, ret);
}

static int lda_sieve_find_scripts(struct lda_sieve_run_context *srctx)
{
	struct mail_deliver_context *mdctx = srctx->mdctx;
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_storage *main_storage;
	const char *sieve_before, *sieve_after, *sieve_discard;
	const char *setting_name;
	enum sieve_error error_code;
	ARRAY_TYPE(sieve_script) script_sequence;
	struct sieve_script *const *scripts;
	unsigned int after_index, count, i;
	int ret = 1;

	/* Find the personal script to execute */

	ret = lda_sieve_get_personal_storage(svinst, mdctx->rcpt_user,
					     &main_storage, &error_code);
	if (ret == 0 && error_code == SIEVE_ERROR_NOT_POSSIBLE)
		return 0;
	if (ret > 0) {
		if (sieve_storage_active_script_open(main_storage,
						     &srctx->main_script,
						     &error_code) < 0) {
			switch (error_code) {
			case SIEVE_ERROR_NOT_FOUND:
				e_debug(sieve_get_event(svinst),
					"User has no active script in storage '%s'",
					sieve_storage_location(main_storage));
				break;
			case SIEVE_ERROR_TEMP_FAILURE:
				e_error(sieve_get_event(svinst),
					"Failed to access active Sieve script in user storage '%s' "
					"(temporary failure)",
					sieve_storage_location(main_storage));
				ret = -1;
				break;
			default:
				e_error(sieve_get_event(svinst),
					"Failed to access active Sieve script in user storage '%s'",
					sieve_storage_location(main_storage));
				break;
			}
		} else if (!sieve_script_is_default(srctx->main_script)) {
			srctx->user_script = srctx->main_script;
		}
		sieve_storage_unref(&main_storage);
	}

	if (ret >= 0 && srctx->main_script == NULL) {
		e_debug(sieve_get_event(svinst),
			"User has no personal script");
	}

	/* Compose script array */

	t_array_init(&script_sequence, 16);

	/* before */
	if (ret >= 0) {
		i = 2;
		setting_name = "sieve_before";
		sieve_before = mail_user_plugin_getenv(
			mdctx->rcpt_user, setting_name);
		while (ret >= 0 &&
		       sieve_before != NULL && *sieve_before != '\0') {
			ret = lda_sieve_multiscript_get_scripts(
				svinst, setting_name, sieve_before,
				&script_sequence, &error_code);
			if (ret < 0 && error_code == SIEVE_ERROR_TEMP_FAILURE) {
				ret = -1;
				break;
			} else if (ret == 0) {
				e_debug(sieve_get_event(svinst),
					"Location for %s not found: %s",
					setting_name, sieve_before);
			}
			ret = 0;
			setting_name = t_strdup_printf("sieve_before%u", i++);
			sieve_before = mail_user_plugin_getenv(
				mdctx->rcpt_user, setting_name);
		}

		if (ret >= 0) {
			scripts = array_get(&script_sequence, &count);
			for (i = 0; i < count; i ++) {
				e_debug(sieve_get_event(svinst),
					"Executed before user's personal Sieve script(%d): %s",
					i+1, sieve_script_location(scripts[i]));
			}
		}
	}

	/* main */
	if (srctx->main_script != NULL) {
		array_append(&script_sequence, &srctx->main_script, 1);

		if (ret >= 0) {
			e_debug(sieve_get_event(svinst),
				"Using the following location for user's Sieve script: %s",
				sieve_script_location(srctx->main_script));
		}
	}

	after_index = array_count(&script_sequence);

	/* after */
	if (ret >= 0) {
		i = 2;
		setting_name = "sieve_after";
		sieve_after = mail_user_plugin_getenv(mdctx->rcpt_user, setting_name);
		while (sieve_after != NULL && *sieve_after != '\0') {
			ret = lda_sieve_multiscript_get_scripts(
				svinst, setting_name, sieve_after,
				&script_sequence, &error_code);
			if (ret < 0 && error_code == SIEVE_ERROR_TEMP_FAILURE) {
				ret = -1;
				break;
			} else if (ret == 0) {
				e_debug(sieve_get_event(svinst),
					"Location for %s not found: %s",
					setting_name, sieve_after);
			}
			ret = 0;
			setting_name = t_strdup_printf("sieve_after%u", i++);
			sieve_after = mail_user_plugin_getenv(
				mdctx->rcpt_user, setting_name);
		}

		if (ret >= 0) {
			scripts = array_get(&script_sequence, &count);
			for ( i = after_index; i < count; i ++ ) {
				e_debug(sieve_get_event(svinst),
					"executed after user's Sieve script(%d): %s",
					i+1, sieve_script_location(scripts[i]));
			}
		}
	}

	/* discard */
	sieve_discard = mail_user_plugin_getenv(
		mdctx->rcpt_user, "sieve_discard");
	if (sieve_discard != NULL && *sieve_discard != '\0') {
		if (sieve_script_create_open(svinst, sieve_discard, NULL,
					     &srctx->discard_script,
					     &error_code) < 0) {
			switch (error_code) {
			case SIEVE_ERROR_NOT_FOUND:
				e_debug(sieve_get_event(svinst),
					"Location for sieve_discard not found: %s",
					sieve_discard);
				break;
			case SIEVE_ERROR_TEMP_FAILURE:
				ret = -1;
				break;
			default:
				break;
			}
		}
	}

	if (ret < 0) {
		mdctx->tempfail_error =
			"Temporarily unable to access necessary Sieve scripts";
	}
	srctx->scripts =
		array_get_modifiable(&script_sequence, &srctx->script_count);
	return ret;
}

static void
lda_sieve_free_scripts(struct lda_sieve_run_context *srctx)
{
	unsigned int i;

	for (i = 0; i < srctx->script_count; i++)
		sieve_script_unref(&srctx->scripts[i]);
	sieve_script_unref(&srctx->discard_script);
}

static void
lda_sieve_init_trace_log(struct lda_sieve_run_context *srctx,
			 const struct smtp_address *mail_from,
			 struct sieve_trace_config *trace_config_r,
			 struct sieve_trace_log **trace_log_r)
{
	struct mail_deliver_context *mdctx = srctx->mdctx;
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_trace_log *trace_log = NULL;

	if (sieve_trace_config_get(svinst, trace_config_r) < 0 ||
	    sieve_trace_log_open(svinst, &trace_log) < 0) {
		i_zero(trace_config_r);
		*trace_log_r = NULL;
		return;
	}

	/* Write header for trace file */
	sieve_trace_log_printf(trace_log,
		"Sieve trace log for message delivery:\n"
		"\n"
		"  Username: %s\n", mdctx->rcpt_user->username);
	if (mdctx->rcpt_user->session_id != NULL) {
		sieve_trace_log_printf(trace_log,
			"  Session ID: %s\n",
			mdctx->rcpt_user->session_id);
	}
	sieve_trace_log_printf(trace_log,
		"  Sender: %s\n"
		"  Final recipient: %s\n"
		"  Default mailbox: %s\n\n",
		smtp_address_encode_path(mail_from),
		smtp_address_encode_path(mdctx->rcpt_to),
		(mdctx->rcpt_default_mailbox != NULL ?
		 mdctx->rcpt_default_mailbox : "INBOX"));

	*trace_log_r = trace_log;
}

static int
lda_sieve_execute(struct lda_sieve_run_context *srctx,
		  struct mail_storage **storage_r)
{
	struct mail_deliver_context *mdctx = srctx->mdctx;
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	struct sieve_trace_config trace_config;
	const struct smtp_address *mail_from;
	struct sieve_trace_log *trace_log;
	const char *error;
	int ret;

	/* Check whether there are any scripts to execute at all */

	if (srctx->script_count == 0) {
		e_debug(sieve_get_event(svinst),
			"No scripts to execute: "
			"reverting to default delivery.");

		/* No error, but no delivery by this plugin either. A return
		   value of <= 0 for a deliver plugin is is considered a
		   failure. In deliver itself, saved_mail and tried_default_save
		   remain unset, meaning that deliver will then attempt the
		   default delivery. We return 0 to signify the lack of a real
		   error.
		 */
		return 0;
	}

	/* Initialize user error handler */

	if (srctx->user_script != NULL) {
		const char *log_path =
			sieve_user_get_log_path(svinst, srctx->user_script);

		if (log_path != NULL) {
			srctx->userlog = log_path;
			srctx->user_ehandler = sieve_logfile_ehandler_create(
				svinst, srctx->userlog,
				LDA_SIEVE_MAX_USER_ERRORS);
		}
	}

	/* Determine return address */

	mail_from = mail_deliver_get_return_address(mdctx);

	/* Initialize trace logging */

	lda_sieve_init_trace_log(srctx, mail_from, &trace_config, &trace_log);

	/* Collect necessary message data */

	i_zero(&msgdata);

	msgdata.mail = mdctx->src_mail;
	msgdata.auth_user = mdctx->rcpt_user->username;
	msgdata.envelope.mail_from = mail_from;
	msgdata.envelope.mail_params = &mdctx->mail_params;
	msgdata.envelope.rcpt_to = mdctx->rcpt_to;
	msgdata.envelope.rcpt_params = &mdctx->rcpt_params;
	(void)mail_get_message_id(msgdata.mail, &msgdata.id);

	srctx->msgdata = &msgdata;

	/* Compose script execution environment */

	if (sieve_script_env_init(&scriptenv, mdctx->rcpt_user, &error) < 0) {
		e_error(sieve_get_event(svinst),
			"Failed to initialize script execution: %s", error);
		if (trace_log != NULL)
			sieve_trace_log_free(&trace_log);
		return -1;
	}

	scriptenv.default_mailbox = mdctx->rcpt_default_mailbox;
	scriptenv.mailbox_autocreate = mdctx->set->lda_mailbox_autocreate;
	scriptenv.mailbox_autosubscribe = mdctx->set->lda_mailbox_autosubscribe;
	scriptenv.smtp_start = lda_sieve_smtp_start;
	scriptenv.smtp_add_rcpt = lda_sieve_smtp_add_rcpt;
	scriptenv.smtp_send = lda_sieve_smtp_send;
	scriptenv.smtp_abort = lda_sieve_smtp_abort;
	scriptenv.smtp_finish = lda_sieve_smtp_finish;
	scriptenv.duplicate_transaction_begin =
		lda_sieve_duplicate_transaction_begin;
	scriptenv.duplicate_transaction_commit =
		lda_sieve_duplicate_transaction_commit;
	scriptenv.duplicate_transaction_rollback =
		lda_sieve_duplicate_transaction_rollback;
	scriptenv.duplicate_mark = lda_sieve_duplicate_mark;
	scriptenv.duplicate_check = lda_sieve_duplicate_check;
	scriptenv.reject_mail = lda_sieve_reject_mail;
	scriptenv.result_amend_log_message = lda_sieve_result_amend_log_message;
	scriptenv.script_context = mdctx;
	scriptenv.trace_log = trace_log;
	scriptenv.trace_config = trace_config;

	i_zero(&estatus);
	scriptenv.exec_status = &estatus;

	srctx->scriptenv = &scriptenv;

	/* Execute script(s) */

	ret = lda_sieve_execute_scripts(srctx);

	/* Record status */

	mdctx->tried_default_save = estatus.tried_default_save;
	*storage_r = estatus.last_storage;

	if (trace_log != NULL)
		sieve_trace_log_free(&trace_log);

	return ret;
}

static int
lda_sieve_deliver_mail(struct mail_deliver_context *mdctx,
		       struct mail_storage **storage_r)
{
	struct lda_sieve_run_context srctx;
	bool debug = mdctx->rcpt_user->set->mail_debug;
	struct sieve_environment svenv;
	int ret = 0;

	/* Initialize run context */

	i_zero(&srctx);
	srctx.mdctx = mdctx;
	(void)mail_user_get_home(mdctx->rcpt_user, &srctx.home_dir);

	/* Initialize Sieve engine */

	i_zero(&svenv);
	svenv.username = mdctx->rcpt_user->username;
	svenv.home_dir = srctx.home_dir;
	svenv.hostname = mdctx->rcpt_user->set->hostname;
	svenv.base_dir = mdctx->rcpt_user->set->base_dir;
	svenv.temp_dir = mdctx->rcpt_user->set->mail_temp_dir;
	svenv.event_parent = mdctx->event;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;
	svenv.location = SIEVE_ENV_LOCATION_MDA;
	svenv.delivery_phase = SIEVE_DELIVERY_PHASE_DURING;

	if (sieve_init(&svenv, &lda_sieve_callbacks, mdctx, debug,
		       &srctx.svinst) < 0)
		return -1;

	/* Initialize master error handler */

	srctx.master_ehandler = sieve_master_ehandler_create(srctx.svinst, 0);

	sieve_error_handler_accept_infolog(srctx.master_ehandler, TRUE);
	sieve_error_handler_accept_debuglog(srctx.master_ehandler, debug);

	*storage_r = NULL;

	/* Find Sieve scripts and run them */

	T_BEGIN {
		if (lda_sieve_find_scripts(&srctx) < 0)
			ret = -1;
		else if (srctx.scripts == NULL)
			ret = 0;
		else
			ret = lda_sieve_execute(&srctx, storage_r);

		lda_sieve_free_scripts(&srctx);
	} T_END;

	/* Clean up */

	if (srctx.user_ehandler != NULL)
		sieve_error_handler_unref(&srctx.user_ehandler);
	sieve_error_handler_unref(&srctx.master_ehandler);
	sieve_deinit(&srctx.svinst);

	return ret;
}

/*
 * Plugin interface
 */

const char *sieve_plugin_version = DOVECOT_ABI_VERSION;
const char sieve_plugin_binary_dependency[] = "lda lmtp";

void sieve_plugin_init(void)
{
	/* Hook into the delivery process */
	next_deliver_mail = mail_deliver_hook_set(lda_sieve_deliver_mail);
}

void sieve_plugin_deinit(void)
{
	/* Remove hook */
	mail_deliver_hook_set(next_deliver_mail);
}
