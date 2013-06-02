/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
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
#include "sieve-script-file.h"

#include "lda-sieve-plugin.h"

#include <stdlib.h>
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

static void *lda_sieve_smtp_open
(const struct sieve_script_env *senv, const char *destination,
	const char *return_path, struct ostream **output_r)
{
	struct mail_deliver_context *dctx =
		(struct mail_deliver_context *) senv->script_context;

	return (void *)smtp_client_open
		(dctx->set, destination, return_path, output_r);
}

static bool lda_sieve_smtp_close
(const struct sieve_script_env *senv ATTR_UNUSED, void *handle)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	return ( smtp_client_close(smtp_client) == 0 );
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

static int lda_sieve_duplicate_check
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

	const struct sieve_message_data *msgdata;
	const struct sieve_script_env *scriptenv;

	struct sieve_error_handler *user_ehandler;
	struct sieve_error_handler *master_ehandler;
	const char *userlog;
};

static const char *lda_sieve_get_personal_location
(struct sieve_instance *svinst, struct mail_user *user)
{
	const char *script_location;

	script_location = mail_user_plugin_getenv(user, "sieve");

	/* userdb may specify Sieve location */
	if (script_location != NULL) {

		if (*script_location == '\0') {
			/* disabled */
			if ( user->mail_debug )
				sieve_sys_debug(svinst, "empty script location, disabled");
			return NULL;
		}
	} else {
		script_location = LDA_SIEVE_DEFAULT_LOCATION;
	}

	return script_location;
}

static const char *lda_sieve_get_default_location
(struct mail_user *user)
{
	const char *script_location;

	/* Use default script location, if one exists */
	script_location = mail_user_plugin_getenv(user, "sieve_default");
	if ( script_location == NULL ) {
		/* For backwards compatibility */
		script_location = mail_user_plugin_getenv(user, "sieve_global_path");
	}

	return script_location;
}

static int lda_sieve_multiscript_get_scripts
(struct sieve_instance *svinst, const char *label, const char *location,
	struct sieve_error_handler *ehandler, ARRAY_TYPE(sieve_scripts) *scripts,
	enum sieve_error *error_r)
{
	struct sieve_directory *sdir;
	ARRAY_TYPE(const_string) script_files;
	const char *const *files;
	unsigned int count, i;
	const char *file;

	// FIXME: make this a generic iteration API
	if ( (sdir=sieve_directory_open(svinst, location, error_r)) == NULL )
		return ( *error_r == SIEVE_ERROR_NOT_FOUND ? 0 : -1 );

	t_array_init(&script_files, 16);

	while ( (file=sieve_directory_get_scriptfile(sdir)) != NULL ) {
		/* Insert into sorted array */

		files = array_get(&script_files, &count);
		for ( i = 0; i < count; i++ ) {
			if ( strcmp(file, files[i]) < 0 )
				break;
		}

		if ( i == count )
			array_append(&script_files, &file, 1);
		else
			array_insert(&script_files, i, &file, 1);
	}

	sieve_directory_close(&sdir);

	files = array_get(&script_files, &count);
	for ( i = 0; i < count; i++ ) {
		struct sieve_script *script = sieve_script_create_open
			(svinst, files[i], NULL, ehandler, error_r);

		if ( script == NULL ) {
			switch ( *error_r ) {
			case SIEVE_ERROR_NOT_FOUND:
				/* Shouldn't normally happen, but the script could have disappeared */
				sieve_sys_warning(svinst,
					"%s script %s doesn't exist", label, files[i]);
				break;
			case SIEVE_ERROR_TEMP_FAILURE:
				sieve_sys_error(svinst,
					"failed to access %s script %s (temporary failure)",
					label, files[i]);
				return -1;
			default:
				sieve_sys_error(svinst,
					"failed to access %s script %s", label, files[i]);
				break;
			}

			continue;
		}

		array_append(scripts, &script, 1);
	}

	*error_r = SIEVE_ERROR_NONE;
	return 1;
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
			"the lda sieve plugin does not have permission "
			"to save global sieve script binaries; "
			"global sieve scripts like %s need to be "
			"pre-compiled using the sievec tool", sieve_script_location(script));
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
			"encountered corrupt binary: re-compiling script %s",
			sieve_script_location(script));
		compile_name = "re-compile";
	} else 	if ( debug ) {
		sieve_sys_debug(svinst,
			"loading script %s", sieve_script_location(script));
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
				sieve_sys_debug(svinst, "script file %s is missing for %s",
					sieve_script_location(script), compile_name);
			}
			break;
		/* Temporary failure */
		case SIEVE_ERROR_TEMP_FAILURE:
			sieve_sys_error(svinst,
				"failed to open script %s for %s (temporary failure)",
				sieve_script_location(script), compile_name);
			break;
		/* Compile failed */
		case SIEVE_ERROR_NOT_VALID:
			if (script == srctx->user_script && srctx->userlog != NULL ) {
				sieve_sys_info(svinst, "failed to %s script %s "
					"(view user logfile %s for more information)",
					compile_name, sieve_script_location(script), srctx->userlog);
				break;
			}
			sieve_sys_error(svinst,	"failed to %s script %s",
				compile_name, sieve_script_location(script));
			break;
		/* Something else */
		default:
			sieve_sys_error(svinst,	"failed to open script %s for %s",
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

	if ( estatus != NULL && estatus->last_storage != NULL ) {
		mail_storage_get_last_error(estatus->last_storage, &mail_error);

		/* Don't bother administrator too much with benign errors */
		if ( mail_error == MAIL_ERROR_NOSPACE ) {
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
			"execution of script %s failed, but implicit keep was successful%s",
			sieve_script_location(script), userlog_notice);
		ret = 1;
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
		error_func(svinst,
			"execution of script %s was aborted due to temporary failure%s",
			sieve_script_location(script), userlog_notice);
		if ( mail_error != MAIL_ERROR_TEMP && mdctx->tempfail_error == NULL ) {
			mdctx->tempfail_error =
				"Execution of Sieve filters was aborted due to temporary failure";
		}
		ret = -1;
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
		sieve_sys_error(svinst,
			"!!BUG!!: binary compiled from %s is still corrupt; "
			"bailing out and reverting to default delivery",
			sieve_script_location(script));
		ret = -1;
		break;
	case SIEVE_EXEC_KEEP_FAILED:
		error_func(svinst,
			"execution of script %s failed with unsuccessful implicit keep%s",
			sieve_script_location(script), userlog_notice);
		ret = -1;
		break;
	default:
		ret = status > 0 ? 1 : -1;
		break;
	}

	return ret;
}

static int lda_sieve_singlescript_execute
(struct lda_sieve_run_context *srctx)
{
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_script *script = srctx->scripts[0];
	bool user_script = ( script == srctx->user_script );
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	bool debug = srctx->mdctx->dest_user->mail_debug;
	enum sieve_compile_flags cpflags = 0;
	enum sieve_runtime_flags rtflags = 0;
	enum sieve_error error;
	int ret;

	if ( user_script ) {
		cpflags |= SIEVE_COMPILE_FLAG_NOGLOBAL;
		rtflags |= SIEVE_RUNTIME_FLAG_NOGLOBAL;
		ehandler = srctx->user_ehandler;
	} else {
		ehandler = srctx->master_ehandler;
	}

	/* Open the script */

	sbin = lda_sieve_open(srctx, script, cpflags, FALSE, &error);
	if ( sbin == NULL ) {
		switch ( error ) {
		case SIEVE_ERROR_NOT_FOUND:
			return 0;
		case SIEVE_ERROR_TEMP_FAILURE:
			return lda_sieve_handle_exec_status
				(srctx, script, SIEVE_EXEC_TEMP_FAILURE);
		default:
			return -1;
		}
	}

	/* Execute */

	if ( debug )
		sieve_sys_debug(svinst, "executing script from %s", sieve_get_source(sbin));

	ret = sieve_execute
		(sbin, srctx->msgdata, srctx->scriptenv, ehandler, rtflags, NULL);

	/* Recompile if corrupt binary */

	if ( ret == SIEVE_EXEC_BIN_CORRUPT && sieve_is_loaded(sbin) ) {
		/* Close corrupt script */

		sieve_close(&sbin);

		/* Recompile */

		sbin = lda_sieve_open(srctx, script, cpflags, TRUE, &error);
		if ( sbin == NULL ) {
			switch ( error ) {
			case SIEVE_ERROR_NOT_FOUND:
				return 0;
			case SIEVE_ERROR_TEMP_FAILURE:
				return lda_sieve_handle_exec_status
					(srctx, script, SIEVE_EXEC_TEMP_FAILURE);
			default:
				return -1;
			}
		}

		/* Execute again */

		if ( debug )
			sieve_sys_debug
				(svinst, "executing script from %s", sieve_get_source(sbin));

		ret = sieve_execute
			(sbin, srctx->msgdata, srctx->scriptenv, ehandler, rtflags, NULL);

		/* Save new version */

		if ( ret != SIEVE_EXEC_BIN_CORRUPT )
			lda_sieve_binary_save(srctx, sbin, script);
	}

	sieve_close(&sbin);

	/* Report status */
	return lda_sieve_handle_exec_status(srctx, script, ret);
}

static int lda_sieve_multiscript_execute
(struct lda_sieve_run_context *srctx)
{
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_script *const *scripts = srctx->scripts;
	unsigned int count = srctx->script_count;
	struct sieve_multiscript *mscript;
	struct sieve_error_handler *ehandler = srctx->master_ehandler;
	bool debug = srctx->mdctx->dest_user->mail_debug;
	struct sieve_script *last_script = NULL;
	bool user_script = FALSE, more = TRUE, compile_error = FALSE;
	enum sieve_error error;
	unsigned int i;
	int ret;

	/* Start execution */

	mscript = sieve_multiscript_start_execute
		(svinst, srctx->msgdata, srctx->scriptenv);

	/* Execute scripts */

	for ( i = 0; i < count && more; i++ ) {
		struct sieve_binary *sbin = NULL;
		struct sieve_script *script = scripts[i];
		enum sieve_compile_flags cpflags = 0;
		enum sieve_runtime_flags rtflags = 0;
		bool final = ( i == count - 1 );

		user_script = ( script == srctx->user_script );
		last_script = script;

		if ( user_script ) {
			cpflags |= SIEVE_COMPILE_FLAG_NOGLOBAL;
			rtflags |= SIEVE_RUNTIME_FLAG_NOGLOBAL;
			ehandler = srctx->user_ehandler;
		} else {
			ehandler = srctx->master_ehandler;
		}

		/* Open */

		if ( debug ) {
			sieve_sys_debug
				(svinst, "opening script %d of %d from %s", i+1, count,
					sieve_script_location(script));
		}

		sbin = lda_sieve_open(srctx, script, cpflags, FALSE, &error);
		if ( sbin == NULL ) {
			compile_error = TRUE;
			break;
		}

		/* Execute */

		if ( debug ) {
			sieve_sys_debug
				(svinst, "executing script from %s", sieve_get_source(sbin));
		}

		more = sieve_multiscript_run(mscript, sbin, ehandler, rtflags, final);

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

				more = sieve_multiscript_run(mscript, sbin, ehandler, rtflags, final);

				/* Save new version */

				if ( more &&
					sieve_multiscript_status(mscript) != SIEVE_EXEC_BIN_CORRUPT ) {
					lda_sieve_binary_save(srctx, sbin, script);
				}
			}
		}

		sieve_close(&sbin);
	}

	/* Finish execution */
	if ( compile_error && error == SIEVE_ERROR_TEMP_FAILURE )
		ret = sieve_multiscript_tempfail(&mscript, ehandler);
	else
		ret = sieve_multiscript_finish(&mscript, ehandler, NULL);

	/* Don't log additional messages about compile failure */
	if ( compile_error && ret == SIEVE_EXEC_FAILURE ) {
		sieve_sys_info(svinst,
			"aborted script execution sequence with successful implicit keep");
		return 1;
	}

	return lda_sieve_handle_exec_status(srctx, last_script, ret);
}

static int lda_sieve_find_scripts(struct lda_sieve_run_context *srctx)
{
	struct mail_deliver_context *mdctx = srctx->mdctx;
	struct sieve_instance *svinst = srctx->svinst;
	struct sieve_error_handler *master_ehandler = srctx->master_ehandler;
	const char *user_location, *default_location, *sieve_before, *sieve_after;
	const char *setting_name;
	enum sieve_error error;
	ARRAY_TYPE(sieve_scripts) script_sequence;
	struct sieve_script *const *scripts;
	bool debug = mdctx->dest_user->mail_debug;
	unsigned int after_index, count, i;
	int ret = 1;

	/* Find the personal script to execute */

	user_location = lda_sieve_get_personal_location(svinst, mdctx->dest_user);
	if ( user_location != NULL ) {
		srctx->user_script = sieve_script_create_open_as
			(svinst, user_location, "main script", master_ehandler, &error);

		if ( srctx->user_script == NULL ) {
			switch ( error ) {
			case SIEVE_ERROR_NOT_FOUND:
				if ( debug ) {
					sieve_sys_debug(svinst, "user's script %s doesn't exist "
						"(trying default script location instead)", user_location);
				}
				break;
			case SIEVE_ERROR_TEMP_FAILURE:
				sieve_sys_error(svinst,
					"failed to access user's Sieve script %s (temporary failure)",
					user_location);
				ret = -1;
				break;
			default:
				sieve_sys_error(svinst,
					"failed to access user's Sieve script %s "
					"(trying default script location instead)",
					user_location);
				break;
			}
		} else {
			srctx->main_script = srctx->user_script;
		}
	}

	if ( ret >= 0 && srctx->user_script == NULL ) {
		default_location = lda_sieve_get_default_location(mdctx->dest_user);
		if ( default_location != NULL ) {
			srctx->main_script = sieve_script_create_open_as
				(svinst, default_location, "main script", master_ehandler, &error);

			if ( srctx->main_script == NULL ) {
				switch ( error ) {
				case SIEVE_ERROR_NOT_FOUND: 
					if ( debug ) {
						sieve_sys_debug(svinst, "default user script %s doesn't exist",
							default_location);
					}
					break;
				case SIEVE_ERROR_TEMP_FAILURE:
					sieve_sys_error(svinst,
						"failed to access default user script %s (temporary failure)",
						default_location);
					ret = -1;
					break;
				default:
					sieve_sys_error(svinst, "failed to access default user script %s",
						default_location);
					break;
				}
			}
		} else {
			sieve_sys_debug(svinst, "no default script configured for user");
		}
	}

	if ( debug && ret >= 0 && srctx->main_script == NULL ) {
		sieve_sys_debug(svinst,
			"user has no valid location for a personal script");
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
				sieve_before, master_ehandler, &script_sequence, &error);
			if ( ret < 0 && error == SIEVE_ERROR_TEMP_FAILURE ) {
				ret = -1;
				break;
			} else if (ret == 0 && debug ) {
				sieve_sys_debug(svinst, "%s location not found: %s",
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
					"executed before user's personal Sieve script(%d): %s",
					i+1, sieve_script_location(scripts[i]));
			}
		}
	}

	/* main */
	if ( srctx->main_script != NULL ) {
		array_append(&script_sequence, &srctx->main_script, 1);

		if ( ret >= 0 && debug ) {
			sieve_sys_debug(svinst,
				"using the following location for user's Sieve script: %s",
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
				sieve_after, master_ehandler, &script_sequence, &error);
			if ( ret < 0 && error == SIEVE_ERROR_TEMP_FAILURE ) {
				ret = -1;
				break;
			} else if (ret == 0 && debug ) {
				sieve_sys_debug(svinst, "%s location not found: %s",
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
	bool debug = mdctx->dest_user->mail_debug;
	int ret;

	/* Check whether there are any scripts to execute at all */

	if ( srctx->script_count == 0 ) {
		if ( debug ) {
			sieve_sys_debug(svinst,
				"no scripts to execute: reverting to default delivery.");
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
			const char *log_path = NULL;

			/* Determine user log file path */
			if ( (log_path=mail_user_plugin_getenv
				(mdctx->dest_user, "sieve_user_log")) == NULL ) {
				const char *path;

				if ( (path=sieve_file_script_get_path(srctx->user_script)) == NULL ) {
					/* Default */
					if ( srctx->home_dir != NULL ) {
						log_path = t_strconcat
							(srctx->home_dir, "/.dovecot.sieve.log", NULL);
					}
				} else {
					/* Use script file as a basic (legacy behavior) */
					log_path = t_strconcat(path, ".log", NULL);
				}
			} else if ( srctx->home_dir != NULL ) {
				/* Expand home dir if necessary */
				if ( log_path[0] == '~' ) {
					log_path = home_expand_tilde(log_path, srctx->home_dir);
				} else if ( log_path[0] != '/' ) {
					log_path = t_strconcat(srctx->home_dir, "/", log_path, NULL);
				}
			}

			if ( log_path != NULL ) {
				srctx->userlog = log_path;
				srctx->user_ehandler = sieve_logfile_ehandler_create
					(svinst, srctx->userlog, LDA_SIEVE_MAX_USER_ERRORS);
			}
		}

		/* Collect necessary message data */

		memset(&msgdata, 0, sizeof(msgdata));

		msgdata.mail = mdctx->src_mail;
		msgdata.return_path = mail_deliver_get_return_address(mdctx);
		msgdata.orig_envelope_to = mdctx->dest_addr;
		msgdata.final_envelope_to = mdctx->final_dest_addr;
		msgdata.auth_user = mdctx->dest_user->username;
		(void)mail_get_first_header(msgdata.mail, "Message-ID", &msgdata.id);

		srctx->msgdata = &msgdata;

		/* Compose script execution environment */

		memset(&scriptenv, 0, sizeof(scriptenv));
		memset(&estatus, 0, sizeof(estatus));

		scriptenv.action_log_format = mdctx->set->deliver_log_format;
		scriptenv.default_mailbox = mdctx->dest_mailbox_name;
		scriptenv.mailbox_autocreate = mdctx->set->lda_mailbox_autocreate;
		scriptenv.mailbox_autosubscribe = mdctx->set->lda_mailbox_autosubscribe;
		scriptenv.user = mdctx->dest_user;
		scriptenv.postmaster_address = mdctx->set->postmaster_address;
		scriptenv.smtp_open = lda_sieve_smtp_open;
		scriptenv.smtp_close = lda_sieve_smtp_close;
		scriptenv.duplicate_mark = lda_sieve_duplicate_mark;
		scriptenv.duplicate_check = lda_sieve_duplicate_check;
		scriptenv.reject_mail = lda_sieve_reject_mail;
		scriptenv.script_context = (void *) mdctx;
		scriptenv.exec_status = &estatus;

		srctx->scriptenv = &scriptenv;

		/* Execute script(s) */

		if ( srctx->script_count == 1 )
			ret = lda_sieve_singlescript_execute(srctx);
		else
			ret = lda_sieve_multiscript_execute(srctx);

		/* Record status */

		mdctx->tried_default_save = estatus.tried_default_save;
		*storage_r = estatus.last_storage;
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

	memset(&srctx, 0, sizeof(srctx));
	srctx.mdctx = mdctx;
	(void)mail_user_get_home(mdctx->dest_user, &srctx.home_dir);

	/* Initialize Sieve engine */

	memset((void*)&svenv, 0, sizeof(svenv));
	svenv.username = mdctx->dest_user->username;
	svenv.home_dir = srctx.home_dir;
	svenv.hostname = mdctx->set->hostname;
	svenv.base_dir = mdctx->dest_user->set->base_dir;
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
		else
			ret = lda_sieve_execute(&srctx, storage_r);
	} T_END;

	/* Clean up */

	for ( i = 0; i < srctx.script_count; i++ )
		sieve_script_unref(&srctx.scripts[i]);
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
