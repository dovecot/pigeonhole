/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"
#include "array.h"
#include "home-expand.h"
#include "mail-storage.h"
#include "mail-deliver.h"
#include "mail-user.h"
#include "duplicate.h"
#include "smtp-client.h"
#include "lda-settings.h"

#include "sieve.h"

#include "lda-sieve-plugin.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

/*
 * Configuration
 */

#define SIEVE_DEFAULT_PATH "~/.dovecot.sieve"

#define LDA_SIEVE_MAX_USER_ERRORS 10
#define LDA_SIEVE_MAX_SYSTEM_ERRORS 100

/*
 * Global variables 
 */

static deliver_mail_func_t *next_deliver_mail;

/*
 * Mail transmission
 */

static void *lda_sieve_smtp_open
(void *script_ctx, const char *destination,
	const char *return_path, FILE **file_r)
{
	return (void *) smtp_client_open
		((struct mail_deliver_context *) script_ctx, destination, 
			return_path, file_r);
}

static bool lda_sieve_smtp_close
(void *script_ctx ATTR_UNUSED, void *handle)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	return ( smtp_client_close(smtp_client) == 0 );
}

/*
 * Plugin implementation
 */

struct lda_sieve_run_context {
	struct mail_deliver_context *mdctx;
	
	const char *const *script_files;
	unsigned int script_count;

	const char *user_script;
	const char *main_script;

	const struct sieve_message_data *msgdata;
	const struct sieve_script_env *scriptenv;

	struct sieve_error_handler *user_ehandler;
	struct sieve_error_handler *master_ehandler;
	const char *userlog;
};

static const char *lda_sieve_get_personal_path(struct mail_user *user)
{
	const char *script_path, *home;

	if ( mail_user_get_home(user, &home) <= 0 )
        home = NULL;

    script_path = mail_user_plugin_getenv(user, "sieve");

	/* userdb may specify Sieve path */
	if (script_path != NULL) {
		if (*script_path == '\0') {
			/* disabled */
			if ( user->mail_debug )
				sieve_sys_info("empty script path, disabled");
			return NULL;
		}

		script_path = home_expand(script_path);

		if (*script_path != '/' && *script_path != '\0') {
			/* relative path. change to absolute. */

			if ( home == NULL || *home == '\0' ) {
				if ( user->mail_debug )
					sieve_sys_info("relative script path, but empty home dir");
				return NULL;
			}

			script_path = t_strconcat(home, "/", script_path, NULL);
		}
	} else {
		if ( home == NULL || *home == '\0' ) {
			sieve_sys_error("per-user script path is unknown. See "
				"http://wiki.dovecot.org/LDA/Sieve#location");
			return NULL;
		}

		script_path = home_expand(SIEVE_DEFAULT_PATH);
	}

	return script_path;
}

static const char *lda_sieve_get_default_path(struct mail_user *user)
{
	const char *script_path;

	/* Use global script path, if one exists */
	script_path = mail_user_plugin_getenv(user, "sieve_global_path");
	if (script_path == NULL) {
		/* For backwards compatibility */
		script_path = mail_user_plugin_getenv(user, "global_script_path");
	}

	return script_path;
}

static void lda_sieve_multiscript_get_scriptfiles
(const char *script_path, ARRAY_TYPE(const_string) *scriptfiles)
{
	struct sieve_directory *sdir = sieve_directory_open(script_path);

	if ( sdir != NULL ) {
		const char *file;

		while ( (file=sieve_directory_get_scriptfile(sdir)) != NULL ) {
			const char *const *scripts;
			unsigned int count, i;

			/* Insert into sorted array */

			scripts = array_get(scriptfiles, &count);
			for ( i = 0; i < count; i++ ) {
				if ( strcmp(file, scripts[i]) < 0 )
					break;			
			}
	
			if ( i == count ) 
				array_append(scriptfiles, &file, 1);
			else
				array_insert(scriptfiles, i, &file, 1);
		}

		sieve_directory_close(&sdir);
	} 
}

static int lda_sieve_open
(struct lda_sieve_run_context *srctx, unsigned int script_index,
	struct sieve_binary **sbin)
{
	const char *script_path = srctx->script_files[script_index];
	const char *script_name =
		( script_path == srctx->main_script ? "main_script" : NULL );
	struct sieve_error_handler *ehandler;
	bool exists = TRUE;
	bool debug = srctx->mdctx->dest_user->mail_debug;
	int ret = 0;

	if ( script_path == srctx->user_script )
		ehandler = srctx->user_ehandler;
	else
		ehandler = srctx->master_ehandler;

	if ( debug )
		sieve_sys_info("opening script %s", script_path);		

	sieve_error_handler_reset(ehandler);

	if ( (*sbin=sieve_open(script_path, script_name, ehandler, &exists)) 
		== NULL ) {

		ret = sieve_get_errors(ehandler) > 0 ? -1 : 0;

		if ( !exists && ret == 0 ) {
			if ( debug )
				sieve_sys_info("script file %s is missing", script_path);
		} else {
			if ( script_path == srctx->user_script && srctx->userlog != NULL ) {
				sieve_sys_error
					("failed to open script %s "
						"(view logfile %s for more information)", 
						script_path, srctx->userlog);
			} else {
				sieve_sys_error
					("failed to open script %s", 
						script_path);
			}
		}

		return ret;
	}

	return 1;
}

static struct sieve_binary *lda_sieve_recompile
(struct lda_sieve_run_context *srctx, unsigned int script_index)
{
	const char *script_path = srctx->script_files[script_index];
	const char *script_name =
		( script_path == srctx->main_script ? "main_script" : NULL );
    struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;

	/* Warn */

	sieve_sys_warning("encountered corrupt binary: recompiling script %s", 
		script_path);

	/* Recompile */	

	if ( script_path == srctx->user_script )
		ehandler = srctx->user_ehandler;
	else
		ehandler = srctx->master_ehandler;

	if ( (sbin=sieve_compile(script_path, script_name, ehandler)) == NULL ) {

		if ( script_path == srctx->user_script && srctx->userlog != NULL ) {
			sieve_sys_error
				("failed to re-compile script %s "
					"(view logfile %s for more information)",
					script_path, srctx->userlog);
		} else {
			sieve_sys_error
				("failed to re-compile script %s", script_path);
		}

		return NULL;
	}

	return sbin;
}

static int lda_sieve_handle_exec_status(const char *script_path, int status)
{
	int ret; 

	switch ( status ) {
	case SIEVE_EXEC_FAILURE:
		sieve_sys_error
			("execution of script %s failed, but implicit keep was successful", 
				script_path);
		ret = 1;
		break;
	case SIEVE_EXEC_BIN_CORRUPT:		
		sieve_sys_error
			("!!BUG!!: binary compiled from %s is still corrupt; "
				"bailing out and reverting to default delivery", 
				script_path);
		ret = -1;
		break;
	case SIEVE_EXEC_KEEP_FAILED:
		sieve_sys_error
			("script %s failed with unsuccessful implicit keep", script_path);
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
	const char *script_file = srctx->script_files[0];
    bool user_script = ( script_file == srctx->user_script );
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	bool debug = srctx->mdctx->dest_user->mail_debug;
	int ret;

	/* Open the script */

	if ( (ret=lda_sieve_open(srctx, 0, &sbin)) <= 0 )
		return ret;

	/* Execute */

	if ( debug )
		sieve_sys_info("executing compiled script %s", script_file);

	if ( user_script ) {
		ehandler = srctx->user_ehandler;
		sieve_error_handler_copy_masterlog(ehandler, TRUE);	
	} else {
		ehandler = srctx->master_ehandler;
	}

	ret = sieve_execute(sbin, srctx->msgdata, srctx->scriptenv, ehandler);

	sieve_error_handler_copy_masterlog(ehandler, FALSE);	

	/* Recompile if corrupt binary */

	if ( ret == SIEVE_EXEC_BIN_CORRUPT ) {
		/* Close corrupt script */

		sieve_close(&sbin);

		/* Recompile */

		if ( (sbin=lda_sieve_recompile(srctx, 0)) == NULL ) {
			return -1;
		}

		/* Execute again */

		if ( user_script )
        	sieve_error_handler_copy_masterlog(ehandler, TRUE);

		ret = sieve_execute(sbin, srctx->msgdata, srctx->scriptenv, ehandler);

		sieve_error_handler_copy_masterlog(ehandler, FALSE);

		/* Save new version */

		if ( ret != SIEVE_EXEC_BIN_CORRUPT )
			sieve_save(sbin, NULL);
	}

	sieve_close(&sbin);

	/* Report status */
	return lda_sieve_handle_exec_status(script_file, ret);
}

static int lda_sieve_multiscript_execute
(struct lda_sieve_run_context *srctx)
{
	const char *const *scripts = srctx->script_files;
	unsigned int count = srctx->script_count;
	struct sieve_multiscript *mscript;
	struct sieve_error_handler *ehandler = srctx->master_ehandler;
	const char *last_script = NULL;
	bool user_script = FALSE;
	unsigned int i;
	int ret = 1; 
	bool more = TRUE;

	/* Start execution */

	mscript = sieve_multiscript_start_execute(srctx->msgdata, srctx->scriptenv);

	/* Execute scripts before main script */

	for ( i = 0; i < count && more; i++ ) {
		struct sieve_binary *sbin = NULL;
		const char *script_file = scripts[i];
		bool final = ( i == count - 1 );

		user_script = ( script_file == srctx->user_script );
		last_script = script_file;		

		if ( user_script )
			ehandler = srctx->user_ehandler;
		else
			ehandler = srctx->master_ehandler;

		/* Open */
	
		if ( (ret=lda_sieve_open(srctx, i, &sbin)) <= 0 )
			break;

		/* Execute */

		if ( user_script )	
			sieve_error_handler_copy_masterlog(ehandler, TRUE);

		more = sieve_multiscript_run(mscript, sbin, ehandler, final);

		sieve_error_handler_copy_masterlog(ehandler, FALSE);

		if ( !more ) {
			if ( sieve_multiscript_status(mscript) == SIEVE_EXEC_BIN_CORRUPT ) {
				/* Close corrupt script */

				sieve_close(&sbin);

				/* Recompile */

				if ( (sbin=lda_sieve_recompile(srctx, i))
					== NULL ) {
					ret = -1;
					break;
				}

				/* Execute again */

				if ( user_script )
					sieve_error_handler_copy_masterlog(ehandler, TRUE);

				more = sieve_multiscript_run(mscript, sbin, ehandler, final);

				sieve_error_handler_copy_masterlog(ehandler, FALSE);

				/* Save new version */

				if ( more && 
					sieve_multiscript_status(mscript) != SIEVE_EXEC_BIN_CORRUPT )
					sieve_save(sbin, NULL);
			}
		}

		sieve_close(&sbin);
	}

	/* Finish execution */

	if ( user_script )	
		sieve_error_handler_copy_masterlog(ehandler, TRUE);

	ret = sieve_multiscript_finish(&mscript, ehandler);

	sieve_error_handler_copy_masterlog(ehandler, FALSE);

	return lda_sieve_handle_exec_status(last_script, ret);
}

static int lda_sieve_run
(struct mail_deliver_context *mdctx, 
	const char *user_script, const char *default_script,
	const ARRAY_TYPE (const_string) *scripts_before,
	const ARRAY_TYPE (const_string) *scripts_after,
	struct mail_storage **storage_r)
{
	ARRAY_TYPE (const_string) scripts;
	struct lda_sieve_run_context srctx;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	int ret = 0;

	*storage_r = NULL;

	/* Initialize */

	memset(&srctx, 0, sizeof(srctx));
	srctx.mdctx = mdctx;

	/* Compose execution sequence */

	t_array_init(&scripts, 32);

	array_append_array(&scripts, scripts_before);

	if ( user_script != NULL ) {
        array_append(&scripts, &user_script, 1);
        srctx.user_script = user_script;
        srctx.main_script = user_script;
    } else if ( default_script != NULL ) {
        array_append(&scripts, &default_script, 1);
        srctx.user_script = NULL;
        srctx.main_script = default_script;
    } else {
        srctx.user_script = NULL;
        srctx.main_script = NULL;
    }

	array_append_array(&scripts, scripts_after);

	/* Create error handlers */

	if ( user_script != NULL ) {
		srctx.userlog = t_strconcat(user_script, ".log", NULL);
		srctx.user_ehandler = sieve_logfile_ehandler_create(srctx.userlog, LDA_SIEVE_MAX_USER_ERRORS);
	}

	srctx.master_ehandler = sieve_master_ehandler_create(LDA_SIEVE_MAX_SYSTEM_ERRORS);
	sieve_error_handler_accept_infolog(srctx.master_ehandler, TRUE);

	/* Collect necessary message data */

	memset(&msgdata, 0, sizeof(msgdata));

	msgdata.mail = mdctx->src_mail;
	msgdata.return_path = mail_deliver_get_return_address(mdctx);
    msgdata.to_address = mdctx->dest_addr;
	msgdata.auth_user = mdctx->dest_user->username;
	(void)mail_get_first_header(msgdata.mail, "Message-ID", &msgdata.id);

	srctx.msgdata = &msgdata;

	/* Compose script execution environment */

	memset(&scriptenv, 0, sizeof(scriptenv));

	scriptenv.default_mailbox = mdctx->dest_mailbox_name;
	scriptenv.mailbox_autocreate = mdctx->set->lda_mailbox_autocreate;
	scriptenv.mailbox_autosubscribe = mdctx->set->lda_mailbox_autosubscribe;
	scriptenv.namespaces = mdctx->dest_user->namespaces;
	scriptenv.username = mdctx->dest_user->username;
	scriptenv.hostname = mdctx->set->hostname;
	scriptenv.postmaster_address = mdctx->set->postmaster_address;
	scriptenv.smtp_open = lda_sieve_smtp_open;
	scriptenv.smtp_close = lda_sieve_smtp_close;
	scriptenv.duplicate_mark = duplicate_mark;
	scriptenv.duplicate_check = duplicate_check;
	scriptenv.script_context = (void *) mdctx;
	scriptenv.exec_status = &estatus;

	srctx.scriptenv = &scriptenv;

	/* Assign script sequence */

	srctx.script_files = array_get(&scripts, &srctx.script_count);

	/* Execute script(s) */

	if ( srctx.script_count == 1 )
		ret = lda_sieve_singlescript_execute(&srctx);
	else
		ret = lda_sieve_multiscript_execute(&srctx);	

	/* Record status */

	mdctx->tried_default_save = estatus.tried_default_save;
	*storage_r = estatus.last_storage;

	/* Clean up */

	if ( srctx.user_ehandler != NULL )
		sieve_error_handler_unref(&srctx.user_ehandler);
	sieve_error_handler_unref(&srctx.master_ehandler);

	return ret;
}

static int lda_sieve_deliver_mail
(struct mail_deliver_context *mdctx, struct mail_storage **storage_r)
{
	const char *user_script, *default_script, *sieve_before, *sieve_after;
	ARRAY_TYPE (const_string) scripts_before;
	ARRAY_TYPE (const_string) scripts_after;
	bool debug = mdctx->dest_user->mail_debug;
	int ret = 0;

	T_BEGIN { 
		struct stat st;

		/* Find the personal script to execute */
	
		user_script = lda_sieve_get_personal_path(mdctx->dest_user);
		default_script = lda_sieve_get_default_path(mdctx->dest_user);

		if ( stat(user_script, &st) < 0 ) {

			if (errno != ENOENT)
				sieve_sys_error("stat(%s) failed: %m "
					"(using global script path in stead)", user_script);
			else if ( debug )
				sieve_sys_info("local script path %s doesn't exist "
					"(using global script path in stead)", user_script);

			user_script = NULL;
		}


		if ( debug ) {
			const char *script = user_script == NULL ? default_script : user_script;

			if ( script == NULL )			
				sieve_sys_info("user has no valid personal script");
			else
				sieve_sys_info("using sieve path for user's script: %s", script);
		}

		/* Check for multiscript */

		t_array_init(&scripts_before, 16);
		t_array_init(&scripts_after, 16);

		sieve_before = mail_user_plugin_getenv(mdctx->dest_user, "sieve_before");
		sieve_after = mail_user_plugin_getenv(mdctx->dest_user, "sieve_after");

		if ( sieve_before != NULL && *sieve_before != '\0' ) {
			lda_sieve_multiscript_get_scriptfiles(sieve_before, &scripts_before);
		}

		if ( sieve_after != NULL && *sieve_after != '\0' ) {
			lda_sieve_multiscript_get_scriptfiles(sieve_after, &scripts_after);
		}

		if ( debug ) {
			const char *const *scriptfiles;
			unsigned int count, i;

			scriptfiles = array_get(&scripts_before, &count);
			for ( i = 0; i < count; i ++ ) {
				sieve_sys_info("executed before user's script(%d): %s", i+1, scriptfiles[i]);				
			}

			scriptfiles = array_get(&scripts_after, &count);
			for ( i = 0; i < count; i ++ ) {
				sieve_sys_info("executed after user's script(%d): %s", i+1, scriptfiles[i]);				
			}
		}
	
		/* Check whether there are any scripts to execute */

		if ( array_count(&scripts_before) == 0 && array_count(&scripts_before) == 0 &&
			user_script == NULL && default_script == NULL ) {
			if ( debug )
				sieve_sys_info("no scripts to execute: reverting to default delivery.");

			/* No error, but no delivery by this plugin either. A return value of <= 0 for a 
			 * deliver plugin is is considered a failure. In deliver itself, saved_mail and 
			 * tried_default_save remain unset, meaning that deliver will then attempt the 
			 * default delivery. We return 0 to signify the lack of a real error. 
			 */
			ret = 0; 
		} else {
			/* Run the script(s) */
				
			ret = lda_sieve_run
                (mdctx, user_script, default_script, &scripts_before, &scripts_after, 
					storage_r);
		}

	} T_END;

	return ret; 
}

/*
 * Plugin interface
 */

void sieve_plugin_init(void)
{
	const char *extensions = NULL;

	/* Initialize Sieve engine */
	sieve_init();

	extensions = getenv("SIEVE_EXTENSIONS");
	if ( extensions != NULL ) {
		sieve_set_extensions(extensions);
	}

	/* Hook into the delivery process */
	next_deliver_mail = deliver_mail;
	deliver_mail = lda_sieve_deliver_mail;
}

void sieve_plugin_deinit(void)
{
	/* Remove hook */
	deliver_mail = next_deliver_mail;

	/* Deinitialize Sieve engine */
	sieve_deinit();
}
