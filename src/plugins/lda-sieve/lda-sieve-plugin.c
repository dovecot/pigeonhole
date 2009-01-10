/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"
#include "array.h"
#include "home-expand.h"
#include "deliver.h"
#include "duplicate.h"
#include "smtp-client.h"

#include "sieve.h"

#include "lda-sieve-plugin.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

/*
 * Configuration
 */

#define SIEVE_SCRIPT_PATH "~/.dovecot.sieve"

#define LDA_SIEVE_MAX_ERRORS 10

/*
 * Global variables 
 */

static deliver_mail_func_t *next_deliver_mail;

static bool lda_sieve_debug = FALSE;

/*
 * Mail transmission
 */

static void *lda_sieve_smtp_open(const char *destination,
	const char *return_path, FILE **file_r)
{
	return (void *) smtp_client_open(destination, return_path, file_r);
}

static bool lda_sieve_smtp_close(void *handle)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;

	return ( smtp_client_close(smtp_client) == 0 );
}

/*
 * Plugin implementation
 */

static const char *lda_sieve_get_path(void)
{
	const char *script_path, *home;
	struct stat st;

	home = getenv("HOME");

	/* userdb may specify Sieve path */
	script_path = getenv("SIEVE");
	if (script_path != NULL) {
		if (*script_path == '\0') {
			/* disabled */
			if ( lda_sieve_debug )
				sieve_sys_info("empty script path, disabled");
			return NULL;
		}

		script_path = home_expand(script_path);

		if (*script_path != '/' && *script_path != '\0') {
			/* relative path. change to absolute. */
			script_path = t_strconcat(getenv("HOME"), "/",
						  script_path, NULL);
		}
	} else {
		if (home == NULL) {
			sieve_sys_error("per-user script path is unknown. See "
				"http://wiki.dovecot.org/LDA/Sieve#location");
			return NULL;
		}

		script_path = home_expand(SIEVE_SCRIPT_PATH);
	}

	if (stat(script_path, &st) < 0) {
		if (errno != ENOENT)
			sieve_sys_error("stat(%s) failed: %m "
				"(using global script path in stead)", script_path);
		else if (getenv("DEBUG") != NULL)
			sieve_sys_info("local script path %s doesn't exist "
				"(using global script path in stead)", script_path);

		/* use global script instead, if one exists */
		script_path = getenv("SIEVE_GLOBAL_PATH");
		if (script_path == NULL) {
			/* for backwards compatibility */
			script_path = getenv("GLOBAL_SCRIPT_PATH");
		}
	}

	return script_path;
}

static int lda_sieve_open
(const char *script_path, struct sieve_error_handler *ehandler, 
	const char *scriptlog, struct sieve_binary **sbin)
{
	bool exists = TRUE;
	int ret = 0;

	if ( lda_sieve_debug )
		sieve_sys_info("opening script %s", script_path);

	if ( (*sbin=sieve_open(script_path, "main script", ehandler, &exists)) 
		== NULL ) {

		ret = sieve_get_errors(ehandler) > 0 ? -1 : 0;

		if ( lda_sieve_debug ) {
			if ( !exists && ret == 0 ) 
				sieve_sys_info
					("script file %s is missing; reverting to default delivery", 
						script_path);
			else
				sieve_sys_info
					("failed to open script %s "
						"(view logfile %s for more information); "
						"reverting to default delivery", 
						script_path, scriptlog);
		}

		sieve_error_handler_unref(&ehandler);
		return ret;
	}

	return 1;
}

static struct sieve_binary *lda_sieve_recompile
(const char *script_path, struct sieve_error_handler *ehandler, 
	const char *scriptlog)
{
	struct sieve_binary *sbin;

	/* Warn */
	sieve_sys_warning("encountered corrupt binary: recompiling script %s", 
		script_path);

	/* Recompile */	

	sieve_error_handler_copy_masterlog(ehandler, FALSE);

	if ( (sbin=sieve_compile(script_path, NULL, ehandler)) == NULL ) {
		sieve_sys_error
				("failed to compile script %s "
					"(view logfile %s for more information); "
					"reverting to default delivery", 
					script_path, scriptlog);
		sieve_error_handler_unref(&ehandler);
		return NULL;
	}

	sieve_error_handler_copy_masterlog(ehandler, TRUE);

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
(const char *script_path, struct sieve_binary **sbin, 
	const struct sieve_message_data *msgdata, const struct sieve_script_env *senv,
	struct sieve_error_handler *ehandler, const char *scriptlog)
{
	int ret;

	if ( lda_sieve_debug )
		sieve_sys_info("executing compiled script %s", script_path);

	/* Execute */

	ret = sieve_execute(*sbin, msgdata, senv, ehandler);

	/* Recompile corrupt binary */

	if ( ret == SIEVE_EXEC_BIN_CORRUPT ) {
		/* Close corrupt script */
		sieve_close(sbin);

		/* Recompile */
		if ( (*sbin=lda_sieve_recompile(script_path, ehandler, scriptlog)) == NULL )
			return -1;

		/* Execute again */
	
		ret = sieve_execute(*sbin, msgdata, senv, ehandler);

		/* Save new version */
		
		if ( ret != SIEVE_EXEC_BIN_CORRUPT )
			sieve_save(*sbin, NULL);
	}

	/* Report status */

	return lda_sieve_handle_exec_status(script_path, ret);
}

static int lda_sieve_multiscript_execute_script
(struct sieve_multiscript *mscript, const char *script, bool final, 
	struct sieve_error_handler *ehandler, const char *scriptlog)
{
	struct sieve_binary *sbin = NULL;
	bool more = FALSE;

	if ( lda_sieve_open(script, ehandler, scriptlog, &sbin) <= 0 )
		return -1;

	if ( !(more=sieve_multiscript_run(mscript, sbin, final)) ) {
		if ( sieve_multiscript_status(mscript) == SIEVE_EXEC_BIN_CORRUPT ) {
			/* Close corrupt script */
			sieve_close(&sbin);

			/* Recompile */

			if ( (sbin=lda_sieve_recompile(script, ehandler, scriptlog)) == NULL )
				return -1;

			/* Execute again */

			more = sieve_multiscript_run(mscript, sbin, final);

			/* Save new version */
	
			if ( more && 
				sieve_multiscript_status(mscript) != SIEVE_EXEC_BIN_CORRUPT )
				sieve_save(sbin, NULL);
		}
	}

	return (int) more;
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

static int lda_sieve_multiscript_execute_scripts
(struct sieve_multiscript *mscript, ARRAY_TYPE(const_string) *scripts, 
	bool final, struct sieve_error_handler *ehandler, const char *scriptlog)
{
	const char *const *scriptfiles;
	unsigned int count, i;
	int ret = 0;
		 
	scriptfiles = array_get(scripts, &count);
	for ( i = 0; i < count; i++ ) {
		if ( (ret=lda_sieve_multiscript_execute_script
			(mscript, scriptfiles[i], ( final && i == count - 1 ), ehandler, 
				scriptlog)) <= 0 )
			return ret;
	}

	return 1;
}

static int lda_sieve_multiscript_execute
(const char *script_path, struct sieve_binary **main_sbin,
	ARRAY_TYPE (const_string) *scripts_before, 
	ARRAY_TYPE (const_string) *scripts_after, 
	const struct sieve_message_data *msgdata, const struct sieve_script_env *senv, 
	struct sieve_error_handler *ehandler,const char *scriptlog)
{
	/* Multiple scripts */
	struct sieve_multiscript *mscript = sieve_multiscript_start_execute
		(msgdata, senv, ehandler);
	int ret = 1; 

	/* Execute scripts before main script */
	ret = lda_sieve_multiscript_execute_scripts
		(mscript, scripts_before, FALSE, ehandler, scriptlog);

	/* Execute main script */
	if ( ret > 0 ) {
		bool final = ( array_count(scripts_after) == 0 );

		if ( !(ret=sieve_multiscript_run(mscript, *main_sbin, final)) ) {

			if ( sieve_multiscript_status(mscript) == SIEVE_EXEC_BIN_CORRUPT ) {
				/* Close corrupt script */
				sieve_close(main_sbin);

				/* Recompile */
				if ( (*main_sbin=lda_sieve_recompile(script_path, ehandler, scriptlog))
					== NULL ) {
					ret = -1;
				} else {

					/* Execute again */
	
					ret = sieve_multiscript_run(mscript, *main_sbin, final);

					/* Save new version */
		
					if ( sieve_multiscript_status(mscript) != SIEVE_EXEC_BIN_CORRUPT )
						sieve_save(*main_sbin, NULL);
				}
			}
		}
	}

	/* Execute scripts after main script */
	if ( ret > 0 )
		ret = lda_sieve_multiscript_execute_scripts
			(mscript, scripts_after, TRUE, ehandler, scriptlog); 

	/* Finish execution */
	ret = sieve_multiscript_finish(&mscript);

	return lda_sieve_handle_exec_status(script_path, ret);
}

static int lda_sieve_run
(struct mail_namespace *namespaces, struct mail *mail, const char *script_path,
	const char *destaddr, const char *username, const char *mailbox,
	struct mail_storage **storage_r)
{
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin = NULL;
	const char *scriptlog, *sieve_before, *sieve_after;
	ARRAY_TYPE (const_string) scripts_before;
	ARRAY_TYPE (const_string) scripts_after;
	int ret = 0;

	*storage_r = NULL;

	/* Create error handler */

	scriptlog = t_strconcat(script_path, ".log", NULL);
	ehandler = sieve_logfile_ehandler_create(scriptlog, LDA_SIEVE_MAX_ERRORS);

	/* Open the script */

	if ( (ret=lda_sieve_open(script_path, ehandler, scriptlog, &sbin)) <= 0 )
		return ret;
	
	/* Log the messages to the system error handlers as well from this moment
	 * on.
	 */
	sieve_error_handler_copy_masterlog(ehandler, TRUE);

	/* Collect necessary message data */
	memset(&msgdata, 0, sizeof(msgdata));
	msgdata.mail = mail;
	msgdata.return_path = deliver_get_return_address(mail);
	msgdata.to_address = destaddr;
	msgdata.auth_user = username;
	(void)mail_get_first_header(mail, "Message-ID", &msgdata.id);

	/* Compose script execution environment */
	memset(&scriptenv, 0, sizeof(scriptenv));
	scriptenv.default_mailbox = mailbox;
	scriptenv.mailbox_autocreate = deliver_set->mailbox_autocreate;
	scriptenv.mailbox_autosubscribe = deliver_set->mailbox_autosubscribe;
	scriptenv.namespaces = namespaces;
	scriptenv.username = username;
	scriptenv.hostname = deliver_set->hostname;
	scriptenv.postmaster_address = deliver_set->postmaster_address;
	scriptenv.smtp_open = lda_sieve_smtp_open;
	scriptenv.smtp_close = lda_sieve_smtp_close;
	scriptenv.duplicate_mark = duplicate_mark;
	scriptenv.duplicate_check = duplicate_check;
	scriptenv.exec_status = &estatus;

	/* Check for multiscript */

	t_array_init(&scripts_after, 16);
	t_array_init(&scripts_before, 16);

	sieve_before = getenv("SIEVE_BEFORE");
	sieve_after = getenv("SIEVE_AFTER");

	if ( sieve_before != NULL && *sieve_before != '\0' )
		lda_sieve_multiscript_get_scriptfiles(sieve_before, &scripts_before);

	if ( sieve_after != NULL && *sieve_after != '\0' )
		lda_sieve_multiscript_get_scriptfiles(sieve_after, &scripts_after);

	if ( array_count(&scripts_before) == 0 && array_count(&scripts_after) == 0 )
		ret = lda_sieve_singlescript_execute
			(script_path, &sbin, &msgdata, &scriptenv, ehandler, scriptlog);
	else
		ret = lda_sieve_multiscript_execute
			(script_path, &sbin, &scripts_before, &scripts_after, &msgdata, 
				&scriptenv, ehandler, scriptlog);	

	/* Record status */

	tried_default_save = estatus.tried_default_save;
	*storage_r = estatus.last_storage;

	/* Clean up */
	if ( sbin != NULL )
		sieve_close(&sbin);
	sieve_error_handler_unref(&ehandler);

	return ret;
}

static int lda_sieve_deliver_mail
(struct mail_namespace *namespaces, struct mail_storage **storage_r, 
	struct mail *mail, const char *destaddr, const char *mailbox)
{
	const char *script_path;
	int ret;

	/* Find the script to execute */
	
	script_path = lda_sieve_get_path();
	if (script_path == NULL) {
		if ( lda_sieve_debug )
			sieve_sys_info("no valid sieve script path specified: "
				"reverting to default delivery.");

		return 0;
	}

	if ( lda_sieve_debug )
		sieve_sys_info("using sieve path: %s", script_path);

	/* Run the script */

	T_BEGIN { 
		ret = lda_sieve_run
			(namespaces, mail, script_path, destaddr, getenv("USER"), mailbox,
				storage_r);
	} T_END;

	return ( ret >= 0 ? 1 : -1 ); 
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

	/* Debug mode */
	lda_sieve_debug = getenv("DEBUG");

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
