/* Copyright (c) 2017-2018 Pigeonhole authors, see the included COPYING file */

#include "imap-common.h"
#include "str.h"
#include "ioloop.h"
#include "time-util.h"
#include "module-context.h"
#include "message-address.h"
#include "mail-user.h"
#include "mail-duplicate.h"
#include "mail-storage-private.h"
#include "iostream-ssl.h"
#include "smtp-submit.h"
#include "sieve.h"
#include "sieve-storage.h"
#include "sieve-script.h"

#include "imap-filter-sieve.h"

#define DUPLICATE_DB_NAME "lda-dupes"

#define IMAP_FILTER_SIEVE_USER_CONTEXT(obj) \
	MODULE_CONTEXT(obj, imap_filter_sieve_user_module)
#define IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(obj) \
	MODULE_CONTEXT_REQUIRE(obj, imap_filter_sieve_user_module)

struct imap_filter_sieve_script {
	struct sieve_script *script;
	struct sieve_binary *binary;

	/* Compile failed once with this error;
	   don't try again for this transaction */
	enum sieve_error compile_error;

	/* Binary corrupt after recompile; don't recompile again */
	bool binary_corrupt:1;
};

struct imap_filter_sieve_user {
	union mail_user_module_context module_ctx;
	struct client *client;

	struct sieve_instance *svinst;
	struct sieve_storage *storage;
	struct sieve_storage *global_storage;

	struct mail_duplicate_db *dup_db;

	struct sieve_error_handler *master_ehandler;
};

static MODULE_CONTEXT_DEFINE_INIT(imap_filter_sieve_user_module,
				  &mail_user_module_register);

/*
 *
 */

static const char *
imap_filter_sieve_get_setting(void *context, const char *identifier)
{
	struct imap_filter_sieve_user *ifsuser = context;
	struct mail_user *user = ifsuser->client->user;

	return mail_user_plugin_getenv(user, identifier);
}

static const struct sieve_callbacks imap_filter_sieve_callbacks = {
	NULL,
	imap_filter_sieve_get_setting
};

static struct sieve_instance *
imap_filter_sieve_get_svinst(struct imap_filter_sieve_context *sctx)
{
	struct mail_user *user = sctx->user;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct sieve_environment svenv;
	const struct mail_storage_settings *mail_set;
	bool debug = user->mail_debug;

	if (ifsuser->svinst != NULL)
		return ifsuser->svinst;

	mail_set = mail_user_set_get_storage_set(user);

	ifsuser->dup_db = mail_duplicate_db_init(user, DUPLICATE_DB_NAME);

	i_zero(&svenv);
	svenv.username = user->username;
	(void)mail_user_get_home(user, &svenv.home_dir);
	svenv.hostname = mail_set->hostname;
	svenv.base_dir = user->set->base_dir;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;
	svenv.location = SIEVE_ENV_LOCATION_MS;
	svenv.delivery_phase = SIEVE_DELIVERY_PHASE_POST;

	ifsuser->svinst = sieve_init(&svenv, &imap_filter_sieve_callbacks,
				     ifsuser, debug);

	ifsuser->master_ehandler = sieve_master_ehandler_create(
		ifsuser->svinst, NULL, 0); // FIXME: prefix?
	sieve_system_ehandler_set(ifsuser->master_ehandler);
	sieve_error_handler_accept_infolog(ifsuser->master_ehandler, TRUE);
	sieve_error_handler_accept_debuglog(ifsuser->master_ehandler, debug);

	return ifsuser->svinst;
}

static int
imap_filter_sieve_get_personal_storage(struct imap_filter_sieve_context *sctx,
				       struct sieve_storage **storage_r,
				       enum mail_error *error_code_r,
				       const char **error_r)
{
	struct mail_user *user = sctx->user;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(user);
	enum sieve_storage_flags storage_flags = 0;
	struct sieve_instance *svinst;
	enum sieve_error error;

	*error_code_r = MAIL_ERROR_NONE;
	*error_r = NULL;

	if (ifsuser->storage != NULL) {
		*storage_r = ifsuser->storage;
		return 0;
	}

	// FIXME: limit interval between retries

	svinst = imap_filter_sieve_get_svinst(sctx);
	ifsuser->storage = sieve_storage_create_main(svinst, user,
						     storage_flags, &error);
	if (ifsuser->storage != NULL) {
		*storage_r = ifsuser->storage;
		return 0;
	}

	switch (error) {
	case SIEVE_ERROR_NOT_POSSIBLE:
		*error_r = "Sieve processing is disabled for this user";
		*error_code_r = MAIL_ERROR_NOTPOSSIBLE;
		break;
	case SIEVE_ERROR_NOT_FOUND:
		*error_r = "Sieve script storage not accessible";
		*error_code_r = MAIL_ERROR_NOTFOUND;
		break;
	default:
		*error_r = t_strflocaltime(MAIL_ERRSTR_CRITICAL_MSG_STAMP,
					   ioloop_time);
		*error_code_r = MAIL_ERROR_TEMP;
		break;
	}

	return -1;
}

static int
imap_filter_sieve_get_global_storage(struct imap_filter_sieve_context *sctx,
				     struct sieve_storage **storage_r,
				     enum mail_error *error_code_r,
				     const char **error_r)
{
	struct mail_user *user = sctx->user;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct sieve_instance *svinst;
	const char *location;
	enum sieve_error error;

	*error_code_r = MAIL_ERROR_NONE;
	*error_r = NULL;

	if (ifsuser->global_storage != NULL) {
		*storage_r = ifsuser->global_storage;
		return 0;
	}

	svinst = imap_filter_sieve_get_svinst(sctx);

	location = mail_user_plugin_getenv(user, "sieve_global");
	if (location == NULL) {
		sieve_sys_info(svinst, "include: sieve_global is unconfigured; "
			"include of `:global' script is therefore not possible");
		*error_code_r = MAIL_ERROR_NOTFOUND;
		*error_r = "No global Sieve scripts available";
		return -1;
	}
	ifsuser->global_storage =
		sieve_storage_create(svinst, location, 0, &error);
	if (ifsuser->global_storage != NULL) {
		*storage_r = ifsuser->global_storage;
		return 0;
	}

	switch (error) {
	case SIEVE_ERROR_NOT_POSSIBLE:
	case SIEVE_ERROR_NOT_FOUND:
		*error_r = "No global Sieve scripts available";
		*error_code_r = MAIL_ERROR_NOTFOUND;
		break;
	default:
		*error_r = t_strflocaltime(MAIL_ERRSTR_CRITICAL_MSG_STAMP,
					   ioloop_time);
		*error_code_r = MAIL_ERROR_TEMP;
		break;
	}

	return -1;
}

/*
 *
 */

struct imap_filter_sieve_context *
imap_filter_sieve_context_create(struct imap_filter_context *ctx,
				 enum imap_filter_sieve_type type)
{
	struct client_command_context *cmd = ctx->cmd;
	struct imap_filter_sieve_context *sctx;

	sctx = p_new(cmd->pool, struct imap_filter_sieve_context, 1);
	sctx->pool = cmd->pool;
	sctx->filter_type = type;
	sctx->user = ctx->cmd->client->user;

	return sctx;
}

void imap_filter_sieve_context_free(struct imap_filter_sieve_context **_sctx)
{
	struct imap_filter_sieve_context *sctx = *_sctx;
	struct imap_filter_sieve_script *scripts;
	unsigned int i;

	*_sctx = NULL;

	if (sctx == NULL)
		return;

	scripts = sctx->scripts;
	for (i = 0; i < sctx->scripts_count; i++) {
		if (scripts[i].binary != NULL)
			sieve_close(&scripts[i].binary);
		if (scripts[i].script != NULL)
			sieve_script_unref(&scripts[i].script);
	}

	str_free(&sctx->errors);
}

/*
 * Error handling
 */

static struct sieve_error_handler *
imap_filter_sieve_create_error_handler(struct imap_filter_sieve_context *sctx)
{
	struct sieve_instance *svinst = imap_filter_sieve_get_svinst(sctx);

	/* Prepare error handler */
	if (sctx->errors == NULL)
		sctx->errors = str_new(default_pool, 1024);
	else
		str_truncate(sctx->errors, 0);
	return sieve_strbuf_ehandler_create(svinst, sctx->errors, TRUE,
		10 /* client->set->_max_compile_errors */);
}

/*
 *
 */

static struct sieve_binary *
imap_sieve_filter_open_script(struct imap_filter_sieve_context *sctx,
			      struct sieve_script *script,
			      enum sieve_compile_flags cpflags,
			      struct sieve_error_handler *user_ehandler,
			      bool recompile, enum sieve_error *error_r)
{
	struct mail_user *user = sctx->user;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct sieve_instance *svinst = imap_filter_sieve_get_svinst(sctx);
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	const char *compile_name = "compile";
	bool debug = user->mail_debug;

	if (recompile) {
		/* Warn */
		sieve_sys_warning(svinst,
			"Encountered corrupt binary: re-compiling script %s",
			sieve_script_location(script));
		compile_name = "re-compile";
	} else if (debug) {
		sieve_sys_debug(svinst,
			"Loading script %s", sieve_script_location(script));
	}

	if (script == sctx->user_script)
		ehandler = user_ehandler;
	else
		ehandler = ifsuser->master_ehandler;
	sieve_error_handler_reset(ehandler);

	/* Load or compile the sieve script */
	if (recompile) {
		sbin = sieve_compile_script(script, ehandler, cpflags, error_r);
	} else {
		sbin = sieve_open_script(script, ehandler, cpflags, error_r);
	}

	/* Handle error */
	if (sbin == NULL) {
		switch (*error_r) {
		/* Script not found */
		case SIEVE_ERROR_NOT_FOUND:
			if (debug) {
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
			if (script == sctx->user_script)
				break;
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

int imap_filter_sieve_compile(struct imap_filter_sieve_context *sctx,
			      string_t **errors_r, bool *have_warnings_r)
{
	struct imap_filter_sieve_script *scripts = sctx->scripts;
	unsigned int count = sctx->scripts_count, i;
	struct sieve_error_handler *ehandler;
	enum sieve_error error;
	int ret = 0;

	*errors_r = NULL;
	*have_warnings_r = FALSE;

	/* Prepare error handler */
	ehandler = imap_filter_sieve_create_error_handler(sctx);

	for (i = 0; i < count; i++) {
		struct sieve_script *script = scripts[i].script;

		i_assert(script != NULL);

		scripts[i].binary =
			imap_sieve_filter_open_script(sctx, script, 0, ehandler,
						     FALSE, &error);
		if (scripts[i].binary == NULL) {
			if (error != SIEVE_ERROR_NOT_VALID) {
				const char *errormsg =
					sieve_script_get_last_error(script, &error);

				if (error != SIEVE_ERROR_NONE) {
					str_truncate(sctx->errors, 0);
					str_append(sctx->errors, errormsg);
				}
			}
			ret = -1;
			break;
		}
	}

	*have_warnings_r = (sieve_get_warnings(ehandler) > 0);
	*errors_r = sctx->errors;

	sieve_error_handler_unref(&ehandler);
	return ret;
}

void imap_filter_sieve_open_input(struct imap_filter_sieve_context *sctx,
				  struct istream *input)
{
	struct sieve_instance *svinst;
	struct sieve_script *script;

	svinst = imap_filter_sieve_get_svinst(sctx);
	script = sieve_data_script_create_from_input(svinst, "script", input);

	sctx->user_script = script;
	sctx->scripts = p_new(sctx->pool, struct imap_filter_sieve_script, 1);
	sctx->scripts_count = 1;
	sctx->scripts[0].script = script;
}

int imap_filter_sieve_open_personal(struct imap_filter_sieve_context *sctx,
				    const char *name,
				    enum mail_error *error_code_r,
				    const char **error_r)
{
	struct sieve_storage *storage;
	struct sieve_script *script;
	enum sieve_error error;

	if (imap_filter_sieve_get_personal_storage(sctx, &storage,
						   error_code_r, error_r) < 0)
		return -1;

	if (name == NULL)
		script = sieve_storage_active_script_open(storage, NULL);
	else
		script = sieve_storage_open_script(storage, name, NULL);
	if (script == NULL) {
		*error_r = sieve_storage_get_last_error(storage, &error);

		switch (error) {
		case SIEVE_ERROR_NOT_FOUND:
			*error_code_r = MAIL_ERROR_NOTFOUND;
			break;
		case SIEVE_ERROR_NOT_POSSIBLE:
			*error_code_r = MAIL_ERROR_NOTPOSSIBLE;
			break;
		default:
			*error_code_r = MAIL_ERROR_TEMP;
		}
		return -1;
	}

	sctx->user_script = script;
	sctx->scripts = p_new(sctx->pool, struct imap_filter_sieve_script, 1);
	sctx->scripts_count = 1;
	sctx->scripts[0].script = script;
	return 0;
}

int imap_filter_sieve_open_global(struct imap_filter_sieve_context *sctx,
				  const char *name,
				  enum mail_error *error_code_r,
				  const char **error_r)
{
	struct sieve_storage *storage;
	struct sieve_script *script;
	enum sieve_error error;

	if (imap_filter_sieve_get_global_storage(sctx, &storage,
						 error_code_r, error_r) < 0)
		return -1;

	script = sieve_storage_open_script(storage, name, NULL);
	if (script == NULL) {
		*error_r = sieve_storage_get_last_error(storage, &error);

		switch (error) {
		case SIEVE_ERROR_NOT_FOUND:
			*error_code_r = MAIL_ERROR_NOTFOUND;
			break;
		case SIEVE_ERROR_NOT_POSSIBLE:
			*error_code_r = MAIL_ERROR_NOTPOSSIBLE;
			break;
		default:
			*error_code_r = MAIL_ERROR_TEMP;
		}
		return -1;
	}

	sctx->user_script = script;
	sctx->scripts = p_new(sctx->pool, struct imap_filter_sieve_script, 1);
	sctx->scripts_count = 1;
	sctx->scripts[0].script = script;
	return 0;
}

/*
 * Mail transmission
 */

static void *
imap_filter_sieve_smtp_start(const struct sieve_script_env *senv,
			     const struct smtp_address *mail_from)
{
	struct imap_filter_sieve_context *sctx = senv->script_context;
	struct mail_user *user = sctx->user;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(user);
	const struct smtp_submit_settings *smtp_set = ifsuser->client->smtp_set;
	struct ssl_iostream_settings ssl_set;

	i_zero(&ssl_set);
	mail_user_init_ssl_client_settings(user, &ssl_set);

	return (void *)smtp_submit_init_simple(smtp_set, &ssl_set, mail_from);
}

static void
imap_filter_sieve_smtp_add_rcpt(const struct sieve_script_env *senv ATTR_UNUSED,
				void *handle,
				const struct smtp_address *rcpt_to)
{
	struct smtp_submit *smtp_submit = handle;

	smtp_submit_add_rcpt(smtp_submit, rcpt_to);
}

static struct ostream *
imap_filter_sieve_smtp_send(const struct sieve_script_env *senv ATTR_UNUSED,
			    void *handle)
{
	struct smtp_submit *smtp_submit = handle;

	return smtp_submit_send(smtp_submit);
}

static void
imap_filter_sieve_smtp_abort(const struct sieve_script_env *senv ATTR_UNUSED,
			     void *handle)
{
	struct smtp_submit *smtp_submit = handle;

	smtp_submit_deinit(&smtp_submit);
}

static int
imap_filter_sieve_smtp_finish(const struct sieve_script_env *senv ATTR_UNUSED,
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

static bool
imap_filter_sieve_duplicate_check(const struct sieve_script_env *senv,
				  const void *id, size_t id_size)
{
	struct imap_filter_sieve_context *sctx = senv->script_context;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(sctx->user);

	return mail_duplicate_check(ifsuser->dup_db,
		id, id_size, senv->user->username);
}

static void
imap_filter_sieve_duplicate_mark(const struct sieve_script_env *senv,
				 const void *id, size_t id_size, time_t time)
{
	struct imap_filter_sieve_context *sctx = senv->script_context;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(sctx->user);

	mail_duplicate_mark(ifsuser->dup_db,
		id, id_size, senv->user->username, time);
}

static void
imap_filter_sieve_duplicate_flush(const struct sieve_script_env *senv)
{
	struct imap_filter_sieve_context *sctx = senv->script_context;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(sctx->user);

	mail_duplicate_db_flush(ifsuser->dup_db);
}

/*
 *
 */

static int
imap_sieve_filter_handle_exec_status(struct imap_filter_sieve_context *sctx,
				     struct sieve_script *script, int status,
				     bool keep,
				     struct sieve_exec_status *estatus)
{
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(sctx->user);
	struct sieve_instance *svinst = ifsuser->svinst;
	sieve_sys_error_func_t error_func, user_error_func;
	enum mail_error mail_error = MAIL_ERROR_NONE;
	int ret = -1;

	error_func = user_error_func = sieve_sys_error;

	if (estatus != NULL && estatus->last_storage != NULL &&
	    estatus->store_failed) {
		(void)mail_storage_get_last_error(estatus->last_storage,
						  &mail_error);

		/* Don't bother administrator too much with benign errors */
		if (mail_error == MAIL_ERROR_NOQUOTA) {
			error_func = sieve_sys_info;
			user_error_func = sieve_sys_info;
		}
	}

	switch ( status ) {
	case SIEVE_EXEC_FAILURE:
		user_error_func(svinst,
			"Execution of script %s failed",
			sieve_script_location(script));
		ret = -1;
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
		error_func(svinst,
			"Execution of script %s was aborted "
			"due to temporary failure",
			sieve_script_location(script));
		ret = -1;
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
		sieve_sys_error(svinst,
			"!!BUG!!: Binary compiled from %s is still corrupt; "
			"bailing out and reverting to default action",
			sieve_script_location(script));
		ret = -1;
		break;
	case SIEVE_EXEC_KEEP_FAILED:
		error_func(svinst,
			"Execution of script %s failed "
			"with unsuccessful implicit keep",
			sieve_script_location(script));
		ret = -1;
		break;
	case SIEVE_EXEC_OK:
		ret = (keep ? 0 : 1);
		break;
	}

	return ret;
}

static int
imap_sieve_filter_run_scripts(struct imap_filter_sieve_context *sctx,
			      struct sieve_error_handler *user_ehandler,
			      const struct sieve_message_data *msgdata,
			      const struct sieve_script_env *scriptenv)
{
	struct mail_user *user = sctx->user;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct sieve_instance *svinst = ifsuser->svinst;
	struct imap_filter_sieve_script *scripts = sctx->scripts;
	unsigned int count = sctx->scripts_count;
	struct sieve_multiscript *mscript;
	struct sieve_error_handler *ehandler;
	struct sieve_script *last_script = NULL;
	bool user_script = FALSE, more = TRUE;
	bool debug = user->mail_debug, keep = TRUE;
	enum sieve_compile_flags cpflags;
	enum sieve_execute_flags exflags;
	enum sieve_error compile_error = SIEVE_ERROR_NONE;
	unsigned int i;
	int ret;

	/* Start execution */
	mscript = sieve_multiscript_start_execute(svinst, msgdata, scriptenv);

	/* Execute scripts */
	for (i = 0; i < count && more; i++) {
		struct sieve_script *script = scripts[i].script;
		struct sieve_binary *sbin = scripts[i].binary;

		cpflags = 0;
		exflags = SIEVE_EXECUTE_FLAG_SKIP_RESPONSES;

		user_script = ( script == sctx->user_script );
		last_script = script;

		if (user_script) {
			cpflags |= SIEVE_COMPILE_FLAG_NOGLOBAL;
			exflags |= SIEVE_EXECUTE_FLAG_NOGLOBAL;
			ehandler = user_ehandler;
		} else {
			ehandler = ifsuser->master_ehandler;
		}

		/* Open */
		i_assert(sbin != NULL);

		/* Execute */
		if (debug) {
			sieve_sys_debug(svinst,
				"Executing script from `%s'",
				sieve_get_source(sbin));
		}
		more = sieve_multiscript_run(mscript,
			sbin, ehandler, ehandler, exflags);

		if (!more) {
			if (!scripts[i].binary_corrupt &&
			    sieve_multiscript_status(mscript)
				== SIEVE_EXEC_BIN_CORRUPT &&
			    sieve_is_loaded(sbin)) {

				/* Close corrupt script */
				sieve_close(&sbin);

				/* Recompile */
				scripts[i].binary = sbin =
					imap_sieve_filter_open_script(sctx,
						script, cpflags, user_ehandler,
						FALSE, &compile_error);
				if ( sbin == NULL ) {
					scripts[i].compile_error = compile_error;
					break;
				}

				/* Execute again */
				more = sieve_multiscript_run(mscript, sbin,
					ehandler, ehandler, exflags);

				/* Save new version */

				if (sieve_multiscript_status(mscript)
					== SIEVE_EXEC_BIN_CORRUPT)
					scripts[i].binary_corrupt = TRUE;
				else if (more)
					(void)sieve_save(sbin, FALSE, NULL);
			}
		}
	}

	/* Finish execution */
	exflags = SIEVE_EXECUTE_FLAG_SKIP_RESPONSES;
	ehandler = (user_ehandler != NULL ?
		user_ehandler : ifsuser->master_ehandler);
	if (compile_error == SIEVE_ERROR_TEMP_FAILURE) {
		ret = sieve_multiscript_tempfail(&mscript, ehandler, exflags);
	} else {
		ret = sieve_multiscript_finish(&mscript, ehandler, exflags,
					       &keep);
	}

	/* Don't log additional messages about compile failure */
	if (compile_error != SIEVE_ERROR_NONE &&
	    ret == SIEVE_EXEC_FAILURE) {
		sieve_sys_info(svinst,
			"Aborted script execution sequence "
			"with successful implicit keep");
		return 1;
	}

	return imap_sieve_filter_handle_exec_status(sctx,
		last_script, ret, keep, scriptenv->exec_status);
}

static int
parse_address(const char *address, const struct smtp_address **addr_r)
{
	struct message_address *msg_addr;
	struct smtp_address *smtp_addr;

	if (message_address_parse_path(pool_datastack_create(),
				       (const unsigned char *)address,
				       strlen(address), &msg_addr) < 0) {
		*addr_r = NULL;
		return -1;
	}
	if (smtp_address_create_from_msg_temp(msg_addr, &smtp_addr) < 0) {
		*addr_r = NULL;
		return -1;
	}

	*addr_r = smtp_addr;
	return 1;
}

static void
imap_sieve_filter_get_msgdata(struct imap_filter_sieve_context *sctx,
			      struct mail *mail,
			      struct sieve_message_data *msgdata_r)
{
	struct sieve_instance *svinst = imap_filter_sieve_get_svinst(sctx);
	struct mail_user *user = sctx->user;
	const char *address, *error;
	const struct smtp_address *mail_from, *rcpt_to;
	struct smtp_address *user_addr;
	int ret;

	mail_from = NULL;
	if ((ret=mail_get_special(mail, MAIL_FETCH_FROM_ENVELOPE,
				  &address)) > 0 &&
	    (ret=parse_address(address, &mail_from)) < 0) {
		sieve_sys_warning(svinst,
			"Failed to parse message FROM_ENVELOPE");
	}
	if (ret <= 0 &&
	    mail_get_first_header(mail, "Return-Path",
				  &address) > 0 &&
	    parse_address(address, &mail_from) < 0) {
		sieve_sys_info(svinst,
			"Failed to parse Return-Path header");
	}

	rcpt_to = NULL;
	if (mail_get_first_header(mail, "Delivered-To",
				  &address) > 0 &&
	    parse_address(address, &rcpt_to) < 0) {
		sieve_sys_info(svinst,
			"Failed to parse Delivered-To header");
	}
	if (rcpt_to == NULL) {
		if (svinst->user_email != NULL)
			rcpt_to = svinst->user_email;
		else if (smtp_address_parse_username(sctx->pool, user->username,
						     &user_addr, &error) < 0) {
			sieve_sys_warning(svinst,
				"Cannot obtain SMTP address from username `%s': %s",
				user->username, error);
		} else {
			if (user_addr->domain == NULL)
				user_addr->domain = svinst->domainname;
			rcpt_to = user_addr;
		}
	}

	// FIXME: maybe parse top Received header.

	i_zero(msgdata_r);
	msgdata_r->mail = mail;
	msgdata_r->envelope.mail_from = mail_from;
	msgdata_r->envelope.rcpt_to = rcpt_to;
	msgdata_r->auth_user = user->username;
	(void)mail_get_first_header(mail, "Message-ID", &msgdata_r->id);
}

int imap_sieve_filter_run_mail(struct imap_filter_sieve_context *sctx,
			       struct mail *mail, string_t **errors_r,
			       bool *have_warnings_r)
{
	struct sieve_instance *svinst = imap_filter_sieve_get_svinst(sctx);
	struct mail_user *user = sctx->user;
	struct sieve_error_handler *user_ehandler;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	struct sieve_trace_config trace_config;
	struct sieve_trace_log *trace_log;
	const char *error;
	int ret;

	*errors_r = NULL;
	*have_warnings_r = FALSE;

	/* Prepare error handler */
	user_ehandler = imap_filter_sieve_create_error_handler(sctx);

	/* Initialize trace logging */

	trace_log = NULL;
	if (sieve_trace_config_get(svinst, &trace_config) >= 0) {
		const char *tr_label = t_strdup_printf
			("%s.%s.%u", user->username,
				mailbox_get_vname(mail->box), mail->uid);
		if (sieve_trace_log_open(svinst, tr_label, &trace_log) < 0)
			i_zero(&trace_config);
	}

	T_BEGIN {
		/* Collect necessary message data */

		imap_sieve_filter_get_msgdata(sctx, mail, &msgdata);

		/* Compose script execution environment */

		if (sieve_script_env_init(&scriptenv, user, &error) < 0) {
			sieve_sys_error(svinst,
				"Failed to initialize script execution: %s",
				error);
			ret = -1;
		} else {
			scriptenv.default_mailbox = mailbox_get_vname(mail->box);
			scriptenv.smtp_start = imap_filter_sieve_smtp_start;
			scriptenv.smtp_add_rcpt = imap_filter_sieve_smtp_add_rcpt;
			scriptenv.smtp_send = imap_filter_sieve_smtp_send;
			scriptenv.smtp_abort = imap_filter_sieve_smtp_abort;
			scriptenv.smtp_finish = imap_filter_sieve_smtp_finish;
			scriptenv.duplicate_mark = imap_filter_sieve_duplicate_mark;
			scriptenv.duplicate_check = imap_filter_sieve_duplicate_check;
			scriptenv.duplicate_flush = imap_filter_sieve_duplicate_flush;
			scriptenv.trace_log = trace_log;
			scriptenv.trace_config = trace_config;
			scriptenv.script_context = sctx;

			i_zero(&estatus);
			scriptenv.exec_status = &estatus;

			/* Execute script(s) */

			ret = imap_sieve_filter_run_scripts(sctx, user_ehandler,
							    &msgdata, &scriptenv);
		}
	} T_END;

	if ( trace_log != NULL )
		sieve_trace_log_free(&trace_log);

	*have_warnings_r = (sieve_get_warnings(user_ehandler) > 0);
	*errors_r = sctx->errors;

	sieve_error_handler_unref(&user_ehandler);

	return ret;
}

/*
 * User
 */

static void imap_filter_sieve_user_deinit(struct mail_user *user)
{
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(user);

	sieve_error_handler_unref(&ifsuser->master_ehandler);

	if (ifsuser->storage != NULL)
		sieve_storage_unref(&ifsuser->storage);
	if (ifsuser->svinst != NULL)
		sieve_deinit(&ifsuser->svinst);
	if (ifsuser->dup_db != NULL)
		mail_duplicate_db_deinit(&ifsuser->dup_db);

	ifsuser->module_ctx.super.deinit(user);
}

static void imap_filter_sieve_user_created(struct mail_user *user)
{
	struct imap_filter_sieve_user *ifsuser;
	struct mail_user_vfuncs *v = user->vlast;

	ifsuser = p_new(user->pool, struct imap_filter_sieve_user, 1);
	ifsuser->module_ctx.super = *v;
	user->vlast = &ifsuser->module_ctx.super;
	v->deinit = imap_filter_sieve_user_deinit;
	MODULE_CONTEXT_SET(user, imap_filter_sieve_user_module, ifsuser);
}

/*
 * Hooks
 */

static struct mail_storage_hooks imap_filter_sieve_mail_storage_hooks = {
	.mail_user_created = imap_filter_sieve_user_created,
};

/*
 * Client
 */

void imap_filter_sieve_client_created(struct client *client)
{
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(client->user);

	ifsuser->client = client;
}

/*
 *
 */

void imap_filter_sieve_init(struct module *module)
{
	mail_storage_hooks_add(module, &imap_filter_sieve_mail_storage_hooks);
}

void imap_filter_sieve_deinit(void)
{
	mail_storage_hooks_remove(&imap_filter_sieve_mail_storage_hooks);
}


