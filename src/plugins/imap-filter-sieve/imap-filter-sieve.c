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
	/* Resource usage exceeded */
	bool rusage_exceeded:1;
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
imap_filter_sieve_get_setting(struct sieve_instance *svinst ATTR_UNUSED,
			      void *context, const char *identifier)
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
	bool debug = user->set->mail_debug;

	if (ifsuser->svinst != NULL)
		return ifsuser->svinst;

	ifsuser->dup_db = mail_duplicate_db_init(user, DUPLICATE_DB_NAME);

	i_zero(&svenv);
	svenv.username = user->username;
	(void)mail_user_get_home(user, &svenv.home_dir);
	svenv.hostname = user->set->hostname;
	svenv.base_dir = user->set->base_dir;
	svenv.event_parent = ifsuser->client->event;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;
	svenv.location = SIEVE_ENV_LOCATION_MS;
	svenv.delivery_phase = SIEVE_DELIVERY_PHASE_POST;

	if (sieve_init(&svenv, &imap_filter_sieve_callbacks, ifsuser, debug,
		       &ifsuser->svinst) < 0)
		return NULL;

	ifsuser->master_ehandler =
		sieve_master_ehandler_create(ifsuser->svinst, 0);
	sieve_error_handler_accept_infolog(ifsuser->master_ehandler, TRUE);
	sieve_error_handler_accept_debuglog(ifsuser->master_ehandler, debug);

	return ifsuser->svinst;
}

static void
imap_filter_sieve_init_trace_log(struct imap_filter_sieve_context *sctx,
				 struct sieve_trace_config *trace_config_r,
				 struct sieve_trace_log **trace_log_r)
{
	struct sieve_instance *svinst = imap_filter_sieve_get_svinst(sctx);
	struct client_command_context *cmd = sctx->filter_context->cmd;
	struct mail_user *user = sctx->user;

	i_assert(svinst != NULL);

	if (sctx->trace_log_initialized) {
		*trace_config_r = sctx->trace_config;
		*trace_log_r = sctx->trace_log;
		return;
	}
	sctx->trace_log_initialized = TRUE;

	if (sieve_trace_config_get(svinst, &sctx->trace_config) < 0 ||
	    sieve_trace_log_open(svinst, &sctx->trace_log) < 0) {
		i_zero(&sctx->trace_config);
		sctx->trace_log = NULL;

		i_zero(trace_config_r);
		*trace_log_r = NULL;
		return;
	}

	/* Write header for trace file */
	sieve_trace_log_printf(
		sctx->trace_log,
		"Sieve trace log for IMAP FILTER=SIEVE:\n"
		"\n"
		"  Username: %s\n", user->username);
	if (user->session_id != NULL) {
		sieve_trace_log_printf(sctx->trace_log,
				       "  Session ID: %s\n",
				       user->session_id);
	}
	sieve_trace_log_printf(
		sctx->trace_log,
		"  Mailbox: %s\n"
		"  Command: %s %s %s\n\n",
		mailbox_get_vname(sctx->filter_context->box),
		cmd->tag, cmd->name,
		cmd->human_args != NULL ? cmd->human_args : "");

	*trace_config_r = sctx->trace_config;
	*trace_log_r = sctx->trace_log;
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
	enum sieve_error error_code;

	*error_code_r = MAIL_ERROR_NONE;
	*error_r = NULL;

	if (ifsuser->storage != NULL) {
		*storage_r = ifsuser->storage;
		return 0;
	}

	// FIXME: limit interval between retries

	svinst = imap_filter_sieve_get_svinst(sctx);
	if (svinst == NULL) {
		*error_r = "Sieve processing is not available";
		*error_code_r = MAIL_ERROR_UNAVAILABLE;
		return -1;
	}

	if (sieve_storage_create_personal(svinst, user, storage_flags,
					  &ifsuser->storage,
					  &error_code) == 0) {
		*storage_r = ifsuser->storage;
		return 0;
	}

	switch (error_code) {
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
	enum sieve_error error_code;

	*error_code_r = MAIL_ERROR_NONE;
	*error_r = NULL;

	if (ifsuser->global_storage != NULL) {
		*storage_r = ifsuser->global_storage;
		return 0;
	}

	svinst = imap_filter_sieve_get_svinst(sctx);
	if (svinst == NULL) {
		*error_r = "Sieve processing is not available";
		*error_code_r = MAIL_ERROR_UNAVAILABLE;
		return -1;
	}

	location = mail_user_plugin_getenv(user, "sieve_global");
	if (location == NULL) {
		e_info(sieve_get_event(svinst),
		       "include: sieve_global is unconfigured; "
		       "include of ':global' script is therefore not possible");
		*error_code_r = MAIL_ERROR_NOTFOUND;
		*error_r = "No global Sieve scripts available";
		return -1;
	}
	if (sieve_storage_create(svinst, svinst->event, location, 0,
				 &ifsuser->global_storage, &error_code) == 0) {
		*storage_r = ifsuser->global_storage;
		return 0;
	}

	switch (error_code) {
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
	sctx->filter_context = ctx;
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
		sieve_script_unref(&scripts[i].script);
	}

	if (sctx->trace_log != NULL)
		sieve_trace_log_free(&sctx->trace_log);

	str_free(&sctx->errors);
}

/*
 * Error handling
 */

static struct sieve_error_handler *
imap_filter_sieve_create_error_handler(struct imap_filter_sieve_context *sctx)
{
	struct sieve_instance *svinst = imap_filter_sieve_get_svinst(sctx);

	i_assert(svinst != NULL);

	/* Prepare error handler */
	if (sctx->errors == NULL)
		sctx->errors = str_new(default_pool, 1024);
	else
		str_truncate(sctx->errors, 0);

	return sieve_strbuf_ehandler_create(
		svinst, sctx->errors, TRUE,
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
			      bool recompile,
			      enum sieve_error *error_code_r)
{
	struct mail_user *user = sctx->user;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct sieve_instance *svinst = imap_filter_sieve_get_svinst(sctx);
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	const char *compile_name = "compile";
	int ret;

	i_assert(svinst != NULL);

	if (recompile) {
		/* Warn */
		e_warning(sieve_get_event(svinst),
			  "Encountered corrupt binary: re-compiling script %s",
			  sieve_script_location(script));
		compile_name = "re-compile";
	} else {
		e_debug(sieve_get_event(svinst), "Loading script %s",
			sieve_script_location(script));
	}

	if (script == sctx->user_script)
		ehandler = user_ehandler;
	else
		ehandler = ifsuser->master_ehandler;
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
				sieve_script_location(script), compile_name);
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
			if (script == sctx->user_script)
				break;
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
		(void)sieve_save(sbin, FALSE, NULL);
	return sbin;
}

int imap_filter_sieve_compile(struct imap_filter_sieve_context *sctx,
			      string_t **errors_r, bool *have_warnings_r)
{
	struct imap_filter_sieve_script *scripts = sctx->scripts;
	unsigned int count = sctx->scripts_count, i;
	struct sieve_error_handler *ehandler;
	enum sieve_error error_code;
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
						      FALSE, &error_code);
		if (scripts[i].binary == NULL) {
			if (error_code != SIEVE_ERROR_NOT_VALID) {
				const char *errormsg =
					sieve_script_get_last_error(
						script, &error_code);
				if (error_code != SIEVE_ERROR_NONE) {
					str_truncate(sctx->errors, 0);
					str_append(sctx->errors, errormsg);
				}
			}
			ret = -1;
			break;
		}
	}

	if (ret < 0 && str_len(sctx->errors) == 0) {
		/* Failed, but no user error was logged: log a generic internal
		   error instead. */
		sieve_internal_error(ehandler, NULL, NULL);
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
	i_assert(svinst != NULL);

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
	enum sieve_error error_code;

	if (imap_filter_sieve_get_personal_storage(sctx, &storage,
						   error_code_r, error_r) < 0)
		return -1;

	int ret;

	if (name == NULL)
		ret = sieve_storage_active_script_open(storage, &script, NULL);
	else
		ret = sieve_storage_open_script(storage, name, &script, NULL);
	if (ret < 0) {
		*error_r = sieve_storage_get_last_error(storage, &error_code);

		switch (error_code) {
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
	enum sieve_error error_code;

	if (imap_filter_sieve_get_global_storage(sctx, &storage,
						 error_code_r, error_r) < 0)
		return -1;

	if (sieve_storage_open_script(storage, name, &script, NULL) < 0) {
		*error_r = sieve_storage_get_last_error(storage, &error_code);

		switch (error_code) {
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
	struct smtp_submit_input submit_input;

	i_zero(&submit_input);

	return smtp_submit_init_simple(&submit_input, smtp_set, mail_from);
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

static void *
imap_filter_sieve_duplicate_transaction_begin(
	const struct sieve_script_env *senv)
{
	struct imap_filter_sieve_context *sctx = senv->script_context;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(sctx->user);

	return mail_duplicate_transaction_begin(ifsuser->dup_db);
}

static void imap_filter_sieve_duplicate_transaction_commit(void **_dup_trans)
{
	struct mail_duplicate_transaction *dup_trans = *_dup_trans;

	*_dup_trans = NULL;

	mail_duplicate_transaction_commit(&dup_trans);
}

static void imap_filter_sieve_duplicate_transaction_rollback(void **_dup_trans)
{
	struct mail_duplicate_transaction *dup_trans = *_dup_trans;

	*_dup_trans = NULL;

	mail_duplicate_transaction_rollback(&dup_trans);
}

static enum sieve_duplicate_check_result
imap_filter_sieve_duplicate_check(void *_dup_trans,
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
imap_filter_sieve_duplicate_mark(void *_dup_trans,
				 const struct sieve_script_env *senv,
				 const void *id, size_t id_size, time_t time)
{
	struct mail_duplicate_transaction *dup_trans = _dup_trans;

	mail_duplicate_mark(dup_trans, id, id_size, senv->user->username, time);
}

/*
 * Result logging
 */

static const char *
imap_filter_sieve_result_amend_log_message(const struct sieve_script_env *senv,
					   enum log_type log_type ATTR_UNUSED,
					   const char *message)
{
	struct imap_filter_sieve_context *sctx = senv->script_context;
	string_t *str;

	if (sctx->mail == NULL)
		return message;

	str = t_str_new(256);
	str_printfa(str, "uid=%u: ", sctx->mail->uid);
	str_append(str, message);
	return str_c(str);
}

/*
 *
 */

static int
imap_sieve_filter_handle_exec_status(struct imap_filter_sieve_context *sctx,
				     struct sieve_script *script, int status,
				     struct sieve_exec_status *estatus,
				     bool *fatal_r)
{
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(sctx->user);
	struct sieve_instance *svinst = ifsuser->svinst;
	enum log_type log_level, user_log_level;
	enum mail_error mail_error = MAIL_ERROR_NONE;
	int ret = -1;

	*fatal_r = FALSE;

	log_level = user_log_level = LOG_TYPE_ERROR;

	if (estatus->last_storage != NULL && estatus->store_failed) {
		(void)mail_storage_get_last_error(estatus->last_storage,
						  &mail_error);

		/* Don't bother administrator too much with benign errors */
		if (mail_error == MAIL_ERROR_NOQUOTA) {
			log_level = LOG_TYPE_INFO;
			user_log_level = LOG_TYPE_INFO;
		}
	}

	switch (status) {
	case SIEVE_EXEC_FAILURE:
		e_log(sieve_get_event(svinst), user_log_level,
		      "Execution of script %s failed",
		      sieve_script_location(script));
		ret = -1;
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
		e_log(sieve_get_event(svinst), log_level,
		      "Execution of script %s was aborted "
		      "due to temporary failure",
		      sieve_script_location(script));
		*fatal_r = TRUE;
		ret = -1;
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
		e_error(sieve_get_event(svinst),
			"!!BUG!!: Binary compiled from %s is still corrupt; "
			"bailing out and reverting to default action",
			sieve_script_location(script));
		*fatal_r = TRUE;
		ret = -1;
		break;
	case SIEVE_EXEC_RESOURCE_LIMIT:
		e_error(sieve_get_event(svinst),
			"Execution of script %s was aborted "
			"due to excessive resource usage",
			sieve_script_location(script));
		*fatal_r = TRUE;
		ret = -1;
		break;
	case SIEVE_EXEC_KEEP_FAILED:
		e_log(sieve_get_event(svinst), log_level,
		      "Execution of script %s failed "
		      "with unsuccessful implicit keep",
		      sieve_script_location(script));
		ret = -1;
		break;
	case SIEVE_EXEC_OK:
		ret = (estatus->keep_original ? 0 : 1);
		break;
	}

	return ret;
}

static int
imap_sieve_filter_run_scripts(struct imap_filter_sieve_context *sctx,
			      struct sieve_error_handler *user_ehandler,
			      const struct sieve_message_data *msgdata,
			      const struct sieve_script_env *scriptenv,
			      bool *fatal_r)
{
	struct mail_user *user = sctx->user;
	struct imap_filter_sieve_user *ifsuser =
		IMAP_FILTER_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct sieve_instance *svinst = ifsuser->svinst;
	struct imap_filter_sieve_script *scripts = sctx->scripts;
	unsigned int count = sctx->scripts_count;
	struct sieve_resource_usage *rusage =
		&scriptenv->exec_status->resource_usage;
	struct sieve_multiscript *mscript;
	struct sieve_error_handler *ehandler;
	struct sieve_script *last_script = NULL;
	bool user_script = FALSE, more = TRUE, rusage_exceeded = FALSE;
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
		int mstatus;

		if (sbin == NULL) {
			e_debug(sieve_get_event(svinst),
				"Skipping script from '%s'",
				sieve_script_location(script));
			continue;
		}

		cpflags = 0;
		exflags = SIEVE_EXECUTE_FLAG_SKIP_RESPONSES;

		user_script = (script == sctx->user_script);
		last_script = script;

		if (scripts[i].rusage_exceeded) {
			rusage_exceeded = TRUE;
			break;
		}

		sieve_resource_usage_init(rusage);
		if (user_script) {
			cpflags |= SIEVE_COMPILE_FLAG_NOGLOBAL;
			exflags |= SIEVE_EXECUTE_FLAG_NOGLOBAL;
			ehandler = user_ehandler;
		} else {
			ehandler = ifsuser->master_ehandler;
		}

		/* Execute */
		e_debug(sieve_get_event(svinst),
			"Executing script from '%s'",
			sieve_get_source(sbin));
		more = sieve_multiscript_run(mscript,
			sbin, ehandler, ehandler, exflags);

		mstatus = sieve_multiscript_status(mscript);
		if (!more && mstatus == SIEVE_EXEC_BIN_CORRUPT &&
		    !scripts[i].binary_corrupt && sieve_is_loaded(sbin)) {
			/* Close corrupt script */
			sieve_close(&sbin);

			/* Recompile */
			scripts[i].binary = sbin =
				imap_sieve_filter_open_script(
					sctx, script, cpflags, user_ehandler,
					FALSE, &compile_error);
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
	exflags = SIEVE_EXECUTE_FLAG_SKIP_RESPONSES;
	ehandler = (user_ehandler != NULL ?
		user_ehandler : ifsuser->master_ehandler);
	if (compile_error == SIEVE_ERROR_TEMP_FAILURE) {
		ret = sieve_multiscript_finish(&mscript, ehandler, exflags,
					       SIEVE_EXEC_TEMP_FAILURE);
	} else if (rusage_exceeded) {
		i_assert(last_script != NULL);
		(void)sieve_multiscript_finish(&mscript, ehandler, exflags,
					       SIEVE_EXEC_TEMP_FAILURE);
		sieve_error(ehandler, sieve_script_name(last_script),
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
		return 0;
	}

	if (last_script == NULL && ret == SIEVE_EXEC_OK)
		return 0;
	i_assert(last_script != NULL); /* At least one script is executed */
	return imap_sieve_filter_handle_exec_status(sctx, last_script, ret,
						    scriptenv->exec_status,
						    fatal_r);
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

int imap_sieve_filter_run_init(struct imap_filter_sieve_context *sctx)
{
	struct sieve_instance *svinst = imap_filter_sieve_get_svinst(sctx);
	struct sieve_script_env *scriptenv = &sctx->scriptenv;
	struct mail_user *user = sctx->user;
	const char *error;

	if (svinst == NULL)
		return -1;
	if (sieve_script_env_init(scriptenv, user, &error) < 0) {
		e_error(sieve_get_event(svinst),
			"Failed to initialize script execution: %s",
			error);
		return -1;
	}

	scriptenv->smtp_start = imap_filter_sieve_smtp_start;
	scriptenv->smtp_add_rcpt = imap_filter_sieve_smtp_add_rcpt;
	scriptenv->smtp_send = imap_filter_sieve_smtp_send;
	scriptenv->smtp_abort = imap_filter_sieve_smtp_abort;
	scriptenv->smtp_finish = imap_filter_sieve_smtp_finish;
	scriptenv->duplicate_transaction_begin =
		imap_filter_sieve_duplicate_transaction_begin;
	scriptenv->duplicate_transaction_commit =
		imap_filter_sieve_duplicate_transaction_commit;
	scriptenv->duplicate_transaction_rollback =
		imap_filter_sieve_duplicate_transaction_rollback;
	scriptenv->duplicate_mark = imap_filter_sieve_duplicate_mark;
	scriptenv->duplicate_check = imap_filter_sieve_duplicate_check;
	scriptenv->script_context = sctx;
	return 0;
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

	i_assert(svinst != NULL);

	mail_from = NULL;
	if ((ret = mail_get_special(mail, MAIL_FETCH_FROM_ENVELOPE,
				    &address)) > 0 &&
	    (ret = parse_address(address, &mail_from)) < 0) {
		e_warning(sieve_get_event(svinst),
			  "Failed to parse message FROM_ENVELOPE");
	}
	if (ret <= 0 &&
	    mail_get_first_header(mail, "Return-Path",
				  &address) > 0 &&
	    parse_address(address, &mail_from) < 0) {
		e_info(sieve_get_event(svinst),
		       "Failed to parse Return-Path header");
	}

	rcpt_to = NULL;
	if (mail_get_first_header(mail, "Delivered-To",
				  &address) > 0 &&
	    parse_address(address, &rcpt_to) < 0) {
		e_info(sieve_get_event(svinst),
		       "Failed to parse Delivered-To header");
	}
	if (rcpt_to == NULL) {
		if (svinst->set->parsed.user_email != NULL)
			rcpt_to = svinst->set->parsed.user_email;
		else if (smtp_address_parse_username(sctx->pool, user->username,
						     &user_addr, &error) < 0) {
			e_warning(sieve_get_event(svinst),
				  "Cannot obtain SMTP address from username '%s': %s",
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
	(void)mail_get_message_id(mail, &msgdata_r->id);
}

int imap_sieve_filter_run_mail(struct imap_filter_sieve_context *sctx,
			       struct mail *mail, string_t **errors_r,
			       bool *have_warnings_r, bool *have_changes_r,
			       bool *fatal_r)
{
	struct sieve_error_handler *user_ehandler;
	struct sieve_message_data msgdata;
	struct sieve_script_env *scriptenv = &sctx->scriptenv;
	struct sieve_exec_status estatus;
	struct sieve_trace_config trace_config;
	struct sieve_trace_log *trace_log;
	int ret;

	*errors_r = NULL;
	*have_warnings_r = FALSE;
	*have_changes_r = FALSE;
	i_zero(&estatus);

	sctx->mail = mail;

	/* Prepare error handler */
	user_ehandler = imap_filter_sieve_create_error_handler(sctx);

	/* Initialize trace logging */
	imap_filter_sieve_init_trace_log(sctx, &trace_config, &trace_log);

	T_BEGIN {
		if (trace_log != NULL) {
			/* Write trace header for message */
			sieve_trace_log_printf(trace_log,
				"Filtering message:\n"
				"\n"
				"  UID: %u\n", mail->uid);
		}

		/* Collect necessary message data */

		imap_sieve_filter_get_msgdata(sctx, mail, &msgdata);

		/* Complete script execution environment */

		scriptenv->default_mailbox = mailbox_get_vname(mail->box);
		scriptenv->result_amend_log_message =
			imap_filter_sieve_result_amend_log_message;
		scriptenv->trace_log = trace_log;
		scriptenv->trace_config = trace_config;
		scriptenv->script_context = sctx;

		scriptenv->exec_status = &estatus;

		/* Execute script(s) */

		ret = imap_sieve_filter_run_scripts(sctx, user_ehandler,
						    &msgdata, scriptenv,
						    fatal_r);
	} T_END;

	if (ret < 0 && str_len(sctx->errors) == 0) {
		/* Failed, but no user error was logged: log a generic internal
		   error instead. */
		sieve_internal_error(user_ehandler, NULL, NULL);
	}

	*have_warnings_r = (sieve_get_warnings(user_ehandler) > 0);
	*have_changes_r = estatus.significant_action_executed;
	*errors_r = sctx->errors;

	sieve_error_handler_unref(&user_ehandler);

	sctx->mail = NULL;

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

	sieve_storage_unref(&ifsuser->storage);
	sieve_storage_unref(&ifsuser->global_storage);
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
