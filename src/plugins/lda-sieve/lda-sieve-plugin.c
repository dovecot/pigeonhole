/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "home-expand.h"
#include "eacces-error.h"
#include "mail-storage.h"
#include "mail-deliver.h"
#include "mail-user.h"
#include "duplicate.h"
#include "smtp-client.h"
#include "mail-send.h"
#include "lda-settings.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "lda-sieve-log.h"
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

static const char *lda_sieve_get_setting
(void *context, const char *identifier)
{
	struct mail_deliver_context *mdctx = (struct mail_deliver_context *)context;
	const char *value = NULL;

	if ( mdctx == NULL )
		return NULL;

	if ( mdctx->dest_user == NULL ||
		(value=mail_user_plugin_getenv(mdctx->dest_user, identifier)) == NULL ) {
		if ( strcmp(identifier, "recipient_delimiter") == 0 )
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

static void *lda_sieve_smtp_start
(const struct sieve_script_env *senv, const char *return_path)
{
	struct mail_deliver_context *dctx =
		(struct mail_deliver_context *) senv->script_context;

	return (void *)smtp_client_init(dctx->set, return_path);
}

static void lda_sieve_smtp_add_rcpt
(const struct sieve_script_env *senv ATTR_UNUSED, void *handle,
	const char *address)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	smtp_client_add_rcpt(smtp_client, address);
}

static struct ostream *lda_sieve_smtp_send
(const struct sieve_script_env *senv ATTR_UNUSED, void *handle)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	return smtp_client_send(smtp_client);
}

static void lda_sieve_smtp_abort
(const struct sieve_script_env *senv ATTR_UNUSED, void *handle)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	smtp_client_abort(&smtp_client);
}

static int lda_sieve_smtp_finish
(const struct sieve_script_env *senv, void *handle,
	const char **error_r)
{
	struct mail_deliver_context *dctx =
		(struct mail_deliver_context *) senv->script_context;
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	return smtp_client_deinit_timeout
		(smtp_client, dctx->timeout_secs, error_r);
}

static int lda_sieve_reject_mail
(const struct sieve_script_env *senv, const char *recipient,
	const char *reason)
{
	struct mail_deliver_context *dctx =
		(struct mail_deliver_context *) senv->script_context;

	return mail_send_rejection(dctx, recipient, reason);
}

/*
 * Duplicate checking
 */

static bool lda_sieve_duplicate_check
(const struct sieve_script_env *senv, const void *id, size_t id_size)
{
	struct mail_deliver_context *dctx =
		(struct mail_deliver_context *) senv->script_context;

	return duplicate_check(dctx->dup_ctx, id, id_size, senv->user->username);
}

static void lda_sieve_duplicate_mark
(const struct sieve_script_env *senv, const void *id, size_t id_size,
	time_t time)
{
	struct mail_deliver_context *dctx =
		(struct mail_deliver_context *) senv->script_context;

	duplicate_mark(dctx->dup_ctx, id, id_size, senv->user->username, time);
}

static void lda_sieve_duplicate_flush
(const struct sieve_script_env *senv)
{
	struct mail_deliver_context *dctx =
		(struct mail_deliver_context *) senv->script_context;
	duplicate_flush(dctx->dup_ctx);
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

static int lda_sieve_get_personal_storage
(struct sieve_instance *svinst, struct mail_user *user,
	struct sieve_storage **storage_r, enum sieve_error *error_r)
{
	*storage_r = sieve_storage_create_main(svinst, user, 0, error_r);
	if (*storage_r == NULL) {
		switch (*error_r) {
		case SIEVE_ERROR_NOT_POSSIBLE:
		case SIEVE_ERROR_NOT_FOUND:
			break;
		case SIEVE_ERROR_TEMP_FAILURE:
			sieve_sys_error(svinst,
				"Failed to access user's personal storage "
				"(temporary failure)");
			return -1;
		default:
			sieve_sys_error(svinst,
				"Failed to access user's personal storage");
			break;
		}
		return 0;
	}
	return 1;
}

static int lda_sieve_multiscript_get_scripts
(struct sieve_instance *svinst, const char *label, const char *location,
	ARRAY_TYPE(sieve_script) *scripts, enum sieve_error *error_r)
{
	struct sieve_script_sequence *seq;
	struct sieve_script *script;
	bool finished = FALSE;
	int ret = 1;

	seq = sieve_script_sequence_create(svinst, location, error_r);
	if ( seq == NULL )
		return ( *error_r == SIEVE_ERROR_NOT_FOUND ? 0 : -1 );

	while ( ret > 0 && !finished ) {
		script = sieve_script_sequence_next(seq, error_r);
		if ( script == NULL ) {
			switch ( *error_r ) {
			case SIEVE_ERROR_NONE:
				finished = TRUE;
				break;
			case SIEVE_ERROR_TEMP_FAILURE:
				sieve_sys_error(svinst,
					"Failed to access %s script from `%s' (temporary failure)",
					label, location);
				ret = -1;
			default:
				break;
			}
			continue;
		}

		array_append(scripts, &script, 1);
	}

	sieve_script_sequence_free(&seq);
	return ret;
}

static void lda_sieve_binary_save
(struct lda_sieve_run_context *srctx, struct sieve_binary *sbin,
	struct sieve_script *script)
{
	enum sieve_error error;

	/* Save binary when compiled */
	if ( sieve_save(sbin, FALSE, &error) < 0 &&
		error == SIEVE_ERROR_NO_PERMISSION && script != srctx->user_script ) {

		/* Cannot save binary for global script */
		sieve_sys_error(srctx->svinst,
			"The LDA Sieve plugin does not have permission "
			"to save global Sieve script binaries; "
			"global Sieve scripts like `%s' need to be "
			"pre-compiled using the sievec tool",
			sieve_script_location(script));
	}
}

static struct sieve_binary *lda_sieve_open
(struct lda_sieve_run_context *srctx, struct sieve_script *script,
	enum sieve_compile_flags cpflags, bool recompile, enum sieve_error *error_r)
{
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	bool debug = srctx->mdctx->dest_user->mail_debug;
	const char *compile_name = "compile";

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

	if ( script == srctx->user_script )
		ehandler = srctx->user_ehandler;
	else
		ehandler = srctx->master_ehandler;

	sieve_error_handler_reset(ehandler);

	if ( recompile )
		sbin = sieve_compile_script(script, ehandler,	cpflags, error_r);
	else 
		sbin = sieve_open_script(script, ehandler, cpflags, error_r);

	/* Load or compile the sieve script */
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
			if (script == srctx->user_script && srctx->userlog != NULL ) {
				sieve_sys_info(svinst,
					"Failed to %s script `%s' "
					"(view user logfile `%s' for more information)",
					compile_name, sieve_script_location(script), srctx->userlog);
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
		lda_sieve_binary_save(srctx, sbin, script);
	return sbin;
}

static int lda_sieve_handle_exec_status
(struct lda_sieve_run_context *srctx, struct sieve_script *script, int status)
{
	struct sieve_instance *svinst = srctx->svinst;
	struct mail_deliver_context *mdctx = srctx->mdctx;
	struct sieve_exec_status *estatus = srctx->scriptenv->exec_status;
	const char *userlog_notice = "";
	sieve_sys_error_func_t error_func, user_error_func; 
	enum mail_error mail_error = MAIL_ERROR_NONE;
	int ret;

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

	if ( script == srctx->user_script && srctx->userlog != NULL ) {
		userlog_notice = t_strdup_printf
			(" (user logfile %s may reveal additional details)", srctx->userlog);
		user_error_func = sieve_sys_info;
	}

	switch ( status ) {
	case SIEVE_EXEC_FAILURE:
		user_error_func(svinst,
			"Execution of script %s failed, but implicit keep was successful%s",
			sieve_script_location(script), userlog_notice);
		ret = 1;
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
		error_func(svinst,
			"Execution of script %s was aborted due to temporary failure%s",
			sieve_script_location(script), userlog_notice);
		if ( mail_error != MAIL_ERROR_TEMP && mdctx->tempfail_error == NULL ) {
			mdctx->tempfail_error =
				"Execution of Sieve filters was aborted due to temporary failure";
		}
		ret = -1;
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
		sieve_sys_error(svinst,
			"!!BUG!!: Binary compiled from %s is still corrupt; "
			"bailing out and reverting to default delivery",
			sieve_script_location(script));
		ret = -1;
		break;
	case SIEVE_EXEC_KEEP_FAILED:
		error_func(svinst,
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

static int lda_sieve_execute_scripts
(struct lda_sieve_run_context *srctx)
{
	struct sieve_instance *svinst = srctx->svinst;
	struct mail_deliver_context *mdctx = srctx->mdctx;
	struct sieve_multiscript *mscript;
	struct sieve_error_handler *exec_ehandler, *action_ehandler;
	bool debug = srctx->mdctx->dest_user->mail_debug;
	struct sieve_script *last_script = NULL;
	bool user_script, discard_script;
	bool compile_error = FALSE;
	enum sieve_error error;
	unsigned int i;
	int ret;

	i_assert( srctx->script_count > 0 );

	/* Start execution */

	mscript = sieve_multiscript_start_execute
		(svinst, srctx->msgdata, srctx->scriptenv);

	/* Execute scripts */

	i = 0;
	discard_script = FALSE;
	for (;;) {
		struct sieve_binary *sbin = NULL;
		struct sieve_script *script;
		enum sieve_compile_flags cpflags = 0;
		enum sieve_execute_flags exflags = 0;
		bool more;

		if ( !discard_script ) {
			/* normal script sequence */
			i_assert( i < srctx->script_count );
			script = srctx->scripts[i];
			i++;
			user_script = ( script == srctx->user_script );
		} else {
			/* discard script */
			script = srctx->discard_script;
			user_script = FALSE;
		}

		i_assert( script != NULL );
		last_script = script;

		if ( user_script ) {
			cpflags |= SIEVE_COMPILE_FLAG_NOGLOBAL;
			exflags |= SIEVE_EXECUTE_FLAG_NOGLOBAL;
			exec_ehandler = srctx->user_ehandler;
		} else {
			exec_ehandler = srctx->master_ehandler;
		}

		/* Open */

		if ( debug ) {
			if ( !discard_script ) {
				sieve_sys_debug(svinst,
					"Opening script %d of %d from `%s'",
					i, srctx->script_count,
					sieve_script_location(script));
			} else {
				sieve_sys_debug(svinst,
					"Opening discard script from `%s'",
					sieve_script_location(script));
			}
		}

		sbin = lda_sieve_open(srctx, script, cpflags, FALSE, &error);
		if ( sbin == NULL ) {
			compile_error = TRUE;
			break;
		}

		/* Execute */

		if ( debug ) {
			sieve_sys_debug
				(svinst, "Executing script from `%s'", sieve_get_source(sbin));
		}

		action_ehandler = lda_sieve_log_ehandler_create
			(exec_ehandler, mdctx);
		if ( !discard_script ) {
			more = sieve_multiscript_run(mscript, sbin,
				exec_ehandler, action_ehandler, exflags);
		} else {
			sieve_multiscript_run_discard(mscript, sbin,
				exec_ehandler, action_ehandler, exflags);
			more = FALSE;
		}
		sieve_error_handler_unref(&action_ehandler);

		if ( !more ) {
			if ( sieve_multiscript_status(mscript) == SIEVE_EXEC_BIN_CORRUPT &&
				sieve_is_loaded(sbin) ) {
				/* Close corrupt script */

				sieve_close(&sbin);

				/* Recompile */

				sbin = lda_sieve_open(srctx, script, cpflags, TRUE, &error);
				if ( sbin == NULL ) {
					compile_error = TRUE;
					break;
				}

				/* Execute again */

				action_ehandler = lda_sieve_log_ehandler_create
					(exec_ehandler, mdctx);
				if ( !discard_script ) {
					more = sieve_multiscript_run(mscript, sbin,
						exec_ehandler, action_ehandler, exflags);
				} else {
					sieve_multiscript_run_discard(mscript, sbin,
						exec_ehandler, action_ehandler, exflags);
				}
				sieve_error_handler_unref(&action_ehandler);

				/* Save new version */

				if ( sieve_multiscript_status(mscript) != SIEVE_EXEC_BIN_CORRUPT )
					lda_sieve_binary_save(srctx, sbin, script);
			}
		}

		sieve_close(&sbin);

		if ( discard_script ) {
			/* Executed discard script, which is always final */
			break;
		} else if ( more ) {
			/* The "keep" action is applied; execute next script */
			i_assert( i <= srctx->script_count );
			if ( i == srctx->script_count ) {
				/* End of normal script sequence */
				break;
			}
		} else if ( sieve_multiscript_will_discard(mscript) &&
			srctx->discard_script != NULL ) {
			/* Mail is set to be discarded, but we have a discard script. */
			discard_script = TRUE;
		} else {
			break;
		}
	}

	/* Finish execution */
	exec_ehandler = (srctx->user_ehandler != NULL ?
		srctx->user_ehandler : srctx->master_ehandler);
	action_ehandler = lda_sieve_log_ehandler_create
		(exec_ehandler, mdctx);
	if ( compile_error && error == SIEVE_ERROR_TEMP_FAILURE ) {
		ret = sieve_multiscript_tempfail
			(&mscript, action_ehandler, 0);
	} else {
		ret = sieve_multiscript_finish
			(&mscript, action_ehandler, 0, NULL);
	}
	sieve_error_handler_unref(&action_ehandler);

	/* Don't log additional messages about compile failure */
	if ( compile_error && ret == SIEVE_EXEC_FAILURE ) {
		sieve_sys_info(svinst,
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
	enum sieve_error error;
	ARRAY_TYPE(sieve_script) script_sequence;
	struct sieve_script *const *scripts;
	bool debug = mdctx->dest_user->mail_debug;
	unsigned int after_index, count, i;
	int ret = 1;

	/* Find the personal script to execute */

	ret = lda_sieve_get_personal_storage
		(svinst, mdctx->dest_user, &main_storage, &error);
	if ( ret == 0 && error == SIEVE_ERROR_NOT_POSSIBLE )
		return 0;
	if ( ret > 0 ) {
		srctx->main_script =
			sieve_storage_active_script_open(main_storage, &error);

		if ( srctx->main_script == NULL ) {
			switch ( error ) {
			case SIEVE_ERROR_NOT_FOUND:
				sieve_sys_debug(svinst,
					"User has no active script in storage `%s'",
					sieve_storage_location(main_storage));
				break;
			case SIEVE_ERROR_TEMP_FAILURE:
				sieve_sys_error(svinst,
					"Failed to access active Sieve script in user storage `%s' "
					"(temporary failure)",
					sieve_storage_location(main_storage));
				ret = -1;
				break;
			default:
				sieve_sys_error(svinst,
					"Failed to access active Sieve script in user storage `%s'",
					sieve_storage_location(main_storage));
				break;
			}
		} else if ( !sieve_script_is_default(srctx->main_script) ) {
			srctx->user_script = srctx->main_script;
		}
		sieve_storage_unref(&main_storage);
	}

	if ( debug && ret >= 0 && srctx->main_script == NULL ) {
		sieve_sys_debug(svinst,
			"User has no personal script");
	}

	/* Compose script array */

	t_array_init(&script_sequence, 16);
	
	/* before */
	if ( ret >= 0 ) {
		i = 2;
		setting_name = "sieve_before";
		sieve_before = mail_user_plugin_getenv(mdctx->dest_user, setting_name);
		while ( ret >= 0 && sieve_before != NULL && *sieve_before != '\0' ) {
			ret = lda_sieve_multiscript_get_scripts(svinst, setting_name,
				sieve_before, &script_sequence, &error);
			if ( ret < 0 && error == SIEVE_ERROR_TEMP_FAILURE ) {
				ret = -1;
				break;
			} else if (ret == 0 && debug ) {
				sieve_sys_debug(svinst, "Location for %s not found: %s",
					setting_name, sieve_before);
			}
			ret = 0;
			setting_name = t_strdup_printf("sieve_before%u", i++);
			sieve_before = mail_user_plugin_getenv(mdctx->dest_user, setting_name);
		}

		if ( ret >= 0 && debug ) {
			scripts = array_get(&script_sequence, &count);
			for ( i = 0; i < count; i ++ ) {
				sieve_sys_debug(svinst,
					"Executed before user's personal Sieve script(%d): %s",
					i+1, sieve_script_location(scripts[i]));
			}
		}
	}

	/* main */
	if ( srctx->main_script != NULL ) {
		array_append(&script_sequence, &srctx->main_script, 1);

		if ( ret >= 0 && debug ) {
			sieve_sys_debug(svinst,
				"Using the following location for user's Sieve script: %s",
				sieve_script_location(srctx->main_script));
		}
	}

	after_index = array_count(&script_sequence);

	/* after */
	if ( ret >= 0 ) {
		i = 2;
		setting_name = "sieve_after";
		sieve_after = mail_user_plugin_getenv(mdctx->dest_user, setting_name);
		while ( sieve_after != NULL && *sieve_after != '\0' ) {
			ret = lda_sieve_multiscript_get_scripts(svinst, setting_name,
				sieve_after, &script_sequence, &error);
			if ( ret < 0 && error == SIEVE_ERROR_TEMP_FAILURE ) {
				ret = -1;
				break;
			} else if (ret == 0 && debug ) {
				sieve_sys_debug(svinst, "Location for %s not found: %s",
					setting_name, sieve_after);
			}
			ret = 0;
			setting_name = t_strdup_printf("sieve_after%u", i++);
			sieve_after = mail_user_plugin_getenv(mdctx->dest_user, setting_name);
		}

		if ( ret >= 0 && debug ) {
			scripts = array_get(&script_sequence, &count);
			for ( i = after_index; i < count; i ++ ) {
				sieve_sys_debug(svinst, "executed after user's Sieve script(%d): %s",
					i+1, sieve_script_location(scripts[i]));
			}
		}
	}

	/* discard */
	sieve_discard = mail_user_plugin_getenv
		(mdctx->dest_user, "sieve_discard");
	if ( sieve_discard != NULL && *sieve_discard != '\0' ) {
		srctx->discard_script = sieve_script_create_open
			(svinst, sieve_discard, NULL, &error);
		if ( srctx->discard_script == NULL ) {
			switch ( error ) {
			case SIEVE_ERROR_NOT_FOUND:
				if (debug ) {
					sieve_sys_debug(svinst,
						"Location for sieve_discard not found: %s",
						sieve_discard);
				}
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

static int lda_sieve_execute
(struct lda_sieve_run_context *srctx, struct mail_storage **storage_r)
{
	struct mail_deliver_context *mdctx = srctx->mdctx;
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	struct sieve_trace_config trace_config;
	struct sieve_trace_log *trace_log;
	bool debug = mdctx->dest_user->mail_debug;
	int ret;

	/* Check whether there are any scripts to execute at all */

	if ( srctx->script_count == 0 ) {
		if ( debug ) {
			sieve_sys_debug(svinst,
				"No scripts to execute: reverting to default delivery.");
		}

		/* No error, but no delivery by this plugin either. A return value of <= 0
		 * for a deliver plugin is is considered a failure. In deliver itself,
		 * saved_mail and tried_default_save remain unset, meaning that deliver
		 * will then attempt the default delivery. We return 0 to signify the lack
		 * of a real error.
		 */
		ret = 0;
	} else {
		/* Initialize user error handler */

		if ( srctx->user_script != NULL ) {
			const char *log_path =
				sieve_user_get_log_path(svinst, srctx->user_script);
	
			if ( log_path != NULL ) {
				srctx->userlog = log_path;
				srctx->user_ehandler = sieve_logfile_ehandler_create
					(svinst, srctx->userlog, LDA_SIEVE_MAX_USER_ERRORS);
			}
		}

		/* Initialize trace logging */

		trace_log = NULL;
		if ( sieve_trace_config_get(svinst, &trace_config) >= 0 &&
			sieve_trace_log_open(svinst, NULL, &trace_log) < 0 )
			i_zero(&trace_config);

		/* Collect necessary message data */

		i_zero(&msgdata);

		msgdata.mail = mdctx->src_mail;
		msgdata.return_path = mail_deliver_get_return_address(mdctx);
		msgdata.orig_envelope_to = mdctx->dest_addr;
		msgdata.final_envelope_to = mdctx->final_dest_addr;
		msgdata.auth_user = mdctx->dest_user->username;
		(void)mail_get_first_header(msgdata.mail, "Message-ID", &msgdata.id);

		srctx->msgdata = &msgdata;

		/* Compose script execution environment */

		i_zero(&scriptenv);
		i_zero(&estatus);

		scriptenv.default_mailbox = mdctx->dest_mailbox_name;
		scriptenv.mailbox_autocreate = mdctx->set->lda_mailbox_autocreate;
		scriptenv.mailbox_autosubscribe = mdctx->set->lda_mailbox_autosubscribe;
		scriptenv.user = mdctx->dest_user;
		scriptenv.postmaster_address = mdctx->set->postmaster_address;
		scriptenv.smtp_start = lda_sieve_smtp_start;
		scriptenv.smtp_add_rcpt = lda_sieve_smtp_add_rcpt;
		scriptenv.smtp_send = lda_sieve_smtp_send;
		scriptenv.smtp_abort = lda_sieve_smtp_abort;
		scriptenv.smtp_finish = lda_sieve_smtp_finish;
		scriptenv.duplicate_mark = lda_sieve_duplicate_mark;
		scriptenv.duplicate_check = lda_sieve_duplicate_check;
		scriptenv.duplicate_flush = lda_sieve_duplicate_flush;
		scriptenv.reject_mail = lda_sieve_reject_mail;
		scriptenv.script_context = (void *) mdctx;
		scriptenv.trace_log = trace_log;
		scriptenv.trace_config = trace_config;
		scriptenv.exec_status = &estatus;

		srctx->scriptenv = &scriptenv;

		/* Execute script(s) */

		ret = lda_sieve_execute_scripts(srctx);

		/* Record status */

		mdctx->tried_default_save = estatus.tried_default_save;
		*storage_r = estatus.last_storage;

		if ( trace_log != NULL )
			sieve_trace_log_free(&trace_log);
	}

	return ret;
}

static int lda_sieve_deliver_mail
(struct mail_deliver_context *mdctx, struct mail_storage **storage_r)
{
	struct lda_sieve_run_context srctx;
	bool debug = mdctx->dest_user->mail_debug;
	struct sieve_environment svenv;
	unsigned int i;
	int ret = 0;

	/* Initialize run context */

	i_zero(&srctx);
	srctx.mdctx = mdctx;
	(void)mail_user_get_home(mdctx->dest_user, &srctx.home_dir);

	/* Initialize Sieve engine */

	memset((void*)&svenv, 0, sizeof(svenv));
	svenv.username = mdctx->dest_user->username;
	svenv.home_dir = srctx.home_dir;
	svenv.hostname = mdctx->set->hostname;
	svenv.base_dir = mdctx->dest_user->set->base_dir;
	svenv.temp_dir = mdctx->dest_user->set->mail_temp_dir;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;
	svenv.location = SIEVE_ENV_LOCATION_MDA;
	svenv.delivery_phase = SIEVE_DELIVERY_PHASE_DURING;

	srctx.svinst = sieve_init(&svenv, &lda_sieve_callbacks, mdctx, debug);

	/* Initialize master error handler */

	srctx.master_ehandler =
		sieve_master_ehandler_create(srctx.svinst, mdctx->session_id, 0);
	sieve_system_ehandler_set(srctx.master_ehandler);

	sieve_error_handler_accept_infolog(srctx.master_ehandler, TRUE);
	sieve_error_handler_accept_debuglog(srctx.master_ehandler, debug);

	*storage_r = NULL;

	/* Find Sieve scripts and run them */

	T_BEGIN {
		if (lda_sieve_find_scripts(&srctx) < 0)
			ret = -1;
		else if ( srctx.scripts == NULL )
			ret = 0;
		else {
			ret = lda_sieve_execute(&srctx, storage_r);
	
			for ( i = 0; i < srctx.script_count; i++ )
				sieve_script_unref(&srctx.scripts[i]);
		}
	} T_END;

	/* Clean up */

	if ( srctx.user_ehandler != NULL )
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
