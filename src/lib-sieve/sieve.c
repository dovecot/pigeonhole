/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "istream.h"
#include "ostream.h"
#include "buffer.h"
#include "time-util.h"
#include "eacces-error.h"
#include "home-expand.h"
#include "hostpid.h"
#include "settings.h"
#include "message-address.h"
#include "mail-user.h"

#include "sieve-settings.old.h"
#include "sieve-extensions.h"
#include "sieve-plugins.h"

#include "sieve-address.h"
#include "sieve-script.h"
#include "sieve-storage-private.h"
#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-actions.h"
#include "sieve-result.h"

#include "sieve-parser.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-binary-dumper.h"

#include "sieve.h"
#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-error-private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>

struct event_category event_category_sieve = {
	.name = "sieve",
};

/*
 * Main Sieve library interface
 */

int sieve_init(const struct sieve_environment *env,
	       const struct sieve_callbacks *callbacks, void *context,
	       bool debug, struct sieve_instance **svinst_r)
{
	struct event *event;
	struct sieve_instance *svinst;
	const char *error;
	struct sieve_settings *set;
	const char *domain;
	pool_t pool;

	*svinst_r = NULL;

	event = event_create(env->event_parent);
	event_add_category(event, &event_category_sieve);
	event_set_forced_debug(event, debug);
	event_set_append_log_prefix(event, "sieve: ");
	event_add_str(event, "user", env->username);
	event_set_ptr(event, SETTINGS_EVENT_FILTER_NAME, SIEVE_SETTINGS_FILTER);
	if (settings_get(event, &sieve_setting_parser_info, 0,
			 &set, &error) < 0) {
		e_error(event, "%s", error);
		event_unref(&event);
		return -1;
	}

	/* Create Sieve engine instance */
	pool = pool_alloconly_create("sieve", 8192);
	svinst = p_new(pool, struct sieve_instance, 1);
	svinst->pool = pool;
	svinst->callbacks = callbacks;
	svinst->context = context;
	svinst->debug = debug;
	svinst->base_dir = p_strdup_empty(pool, env->base_dir);
	svinst->username = p_strdup_empty(pool, env->username);
	svinst->home_dir = p_strdup_empty(pool, env->home_dir);
	svinst->temp_dir = p_strdup_empty(pool, env->temp_dir);
	svinst->flags = env->flags;
	svinst->env_location = env->location;
	svinst->delivery_phase = env->delivery_phase;
	svinst->event = event;
	svinst->set = set;

	/* Determine domain */
	if (env->domainname != NULL && *(env->domainname) != '\0')
		domain = env->domainname;
	else {
		/* Fall back to parsing username localpart@domain */
		domain = svinst->username == NULL ? NULL :
			strchr(svinst->username, '@');
		if (domain == NULL || *(domain+1) == '\0') {
			/* Fall back to parsing hostname host.domain */
			domain = (env->hostname != NULL ?
				  strchr(env->hostname, '.') : NULL);
			if (domain == NULL || *(domain+1) == '\0' ||
			    strchr(domain+1, '.') == NULL) {
				/* Fall back to bare hostname */
				domain = env->hostname;
			} else {
				domain++;
			}
		} else {
			domain++;
		}
	}
	svinst->hostname = p_strdup_empty(pool, env->hostname);
	svinst->domainname = p_strdup(pool, domain);

	sieve_errors_init(svinst);

	e_debug(event, "%s version %s initializing",
		PIGEONHOLE_NAME, PIGEONHOLE_VERSION_FULL);

	/* Initialize extensions */
	if (sieve_extensions_init(svinst) < 0) {
		sieve_deinit(&svinst);
		return -1;
	}

	/* Initialize storage classes */
	sieve_storages_init(svinst);

	/* Initialize plugins */
	sieve_plugins_load(svinst, NULL, NULL);

	/* Load extensions */
	sieve_extensions_load(svinst);

	*svinst_r = svinst;
	return 0;
}

void sieve_deinit(struct sieve_instance **_svinst)
{
	struct sieve_instance *svinst = *_svinst;

	if (svinst == NULL)
		return;
	*_svinst = NULL;

	sieve_plugins_unload(svinst);
	sieve_storages_deinit(svinst);
	sieve_extensions_deinit(svinst);
	sieve_errors_deinit(svinst);

	settings_free(svinst->set);
	event_unref(&svinst->event);

	pool_unref(&(svinst)->pool);
}

int sieve_settings_reload(struct sieve_instance *svinst)
{
	struct sieve_settings *set;
	const char *error;

	if (settings_get(svinst->event, &sieve_setting_parser_info, 0,
			 &set, &error) < 0) {
		e_error(svinst->event, "%s", error);
		return -1;
	}

	settings_free(svinst->set);
	svinst->set = set;
	return 0;
}

void sieve_set_extensions(struct sieve_instance *svinst, const char *extensions)
{
	sieve_extensions_set_string(svinst, extensions, FALSE, FALSE);
}

const char *
sieve_get_capabilities(struct sieve_instance *svinst, const char *name)
{
	if (name == NULL || *name == '\0')
		return sieve_extensions_get_string(svinst);

	return sieve_extension_capabilities_get_string(svinst, name);
}

struct event *sieve_get_event(struct sieve_instance *svinst)
{
	return svinst->event;
}

/*
 * Low-level compiler functions
 */

struct sieve_ast *
sieve_parse(struct sieve_script *script, struct sieve_error_handler *ehandler,
	    enum sieve_error *error_code_r)
{
	struct sieve_parser *parser;
	struct sieve_ast *ast = NULL;

	sieve_error_args_init(&error_code_r, NULL);

	/* Parse */
	parser = sieve_parser_create(script, ehandler, error_code_r);
	if (parser == NULL)
		return NULL;

 	if (!sieve_parser_run(parser, &ast))
 		ast = NULL;
 	else
		sieve_ast_ref(ast);

	sieve_parser_free(&parser);

	if (ast == NULL)
		*error_code_r = SIEVE_ERROR_NOT_VALID;
	return ast;
}

bool sieve_validate(struct sieve_ast *ast, struct sieve_error_handler *ehandler,
		    enum sieve_compile_flags flags,
		    enum sieve_error *error_code_r)
{
	bool result = TRUE;
	struct sieve_validator *validator;

	sieve_error_args_init(&error_code_r, NULL);

	validator = sieve_validator_create(ast, ehandler, flags);
	if (!sieve_validator_run(validator))
		result = FALSE;

	sieve_validator_free(&validator);

	if (!result)
		*error_code_r = SIEVE_ERROR_NOT_VALID;
	return result;
}

static struct sieve_binary *
sieve_generate(struct sieve_ast *ast, struct sieve_error_handler *ehandler,
	       enum sieve_compile_flags flags, enum sieve_error *error_code_r)
{
	struct sieve_generator *generator;
	struct sieve_binary *sbin = NULL;

	sieve_error_args_init(&error_code_r, NULL);

	generator = sieve_generator_create(ast, ehandler, flags);
	sbin = sieve_generator_run(generator, NULL);

	sieve_generator_free(&generator);

	if (sbin == NULL)
		*error_code_r = SIEVE_ERROR_NOT_VALID;
	return sbin;
}

/*
 * Sieve compilation
 */

int sieve_compile_script(struct sieve_script *script,
			 struct sieve_error_handler *ehandler,
			 enum sieve_compile_flags flags,
			 struct sieve_binary **sbin_r,
			 enum sieve_error *error_code_r)
{
	struct sieve_ast *ast;
	struct sieve_binary *sbin;
	bool no_error_result = (error_code_r == NULL);

	*sbin_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);

	/* Parse */
	ast = sieve_parse(script, ehandler, error_code_r);
	if (ast == NULL) {
		switch (*error_code_r) {
		case SIEVE_ERROR_NOT_FOUND:
			if (no_error_result) {
				sieve_error(ehandler, sieve_script_name(script),
					    "script not found");
			}
			break;
		default:
			sieve_error(ehandler, sieve_script_name(script),
				    "parse failed");
		}
		return -1;
	}

	/* Validate */
	if (!sieve_validate(ast, ehandler, flags, error_code_r)) {
		sieve_error(ehandler, sieve_script_name(script),
			    "validation failed");

 		sieve_ast_unref(&ast);
		return -1;
 	}

	/* Generate */
	sbin = sieve_generate(ast, ehandler, flags, error_code_r);
	if (sbin == NULL) {
		sieve_error(ehandler, sieve_script_name(script),
			    "code generation failed");
		sieve_ast_unref(&ast);
		return -1;
	}

	/* Cleanup */
	sieve_ast_unref(&ast);
	*sbin_r = sbin;
	return 0;
}

int sieve_compile(struct sieve_instance *svinst, const char *script_location,
		  const char *script_name, struct sieve_error_handler *ehandler,
		  enum sieve_compile_flags flags, struct sieve_binary **sbin_r,
		  enum sieve_error *error_code_r)
{
	struct sieve_script *script;
	bool no_error_result = (error_code_r == NULL);

	*sbin_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);

	if (sieve_script_create_open(svinst, script_location, script_name,
				     &script, error_code_r) < 0) {
		switch (*error_code_r) {
		case SIEVE_ERROR_NOT_FOUND:
			if (no_error_result) {
				sieve_error(ehandler, script_name,
					    "script not found");
			}
			break;
		default:
			sieve_internal_error(ehandler, script_name,
					     "failed to open script");
		}
		return -1;
	}

	if (sieve_compile_script(script, ehandler, flags,
				 sbin_r, error_code_r) < 0) {
		sieve_script_unref(&script);
		return -1;
	}

	e_debug(svinst->event, "Script '%s' successfully compiled",
		sieve_script_label(script));

	sieve_script_unref(&script);
	return 0;
}

/*
 * Sieve runtime
 */

static int
sieve_run(struct sieve_binary *sbin, struct sieve_result *result,
	  struct sieve_execute_env *eenv, struct sieve_error_handler *ehandler)
{
	struct sieve_interpreter *interp;
	int ret = 0;

	/* Create the interpreter */
	interp = sieve_interpreter_create(sbin, NULL, eenv, ehandler);
	if (interp == NULL)
		return SIEVE_EXEC_BIN_CORRUPT;

	/* Run the interpreter */
	ret = sieve_interpreter_run(interp, result);

	/* Free the interpreter */
	sieve_interpreter_free(&interp);

	return ret;
}

/*
 * Reading/writing sieve binaries
 */

int sieve_load(struct sieve_instance *svinst, const char *bin_path,
	       struct sieve_binary **sbin_r, enum sieve_error *error_code_r)
{
	return sieve_binary_open(svinst, bin_path, NULL, sbin_r, error_code_r);
}

static int
sieve_open_script_real(struct sieve_script *script,
		       struct sieve_error_handler *ehandler,
		       enum sieve_compile_flags flags,
		       struct sieve_binary **sbin_r,
		       enum sieve_error *error_code_r)
{
	struct sieve_instance *svinst = sieve_script_svinst(script);
	struct sieve_resource_usage rusage;
	struct sieve_binary *sbin;
	const char *error = NULL;
	int ret;

	sieve_resource_usage_init(&rusage);

	/* Try to open the matching binary */
	if (sieve_script_binary_load(script, &sbin, error_code_r) == 0) {
		sieve_binary_get_resource_usage(sbin, &rusage);

		/* Ok, it exists; now let's see if it is up to date */
		if (!sieve_resource_usage_is_excessive(svinst, &rusage) &&
		    !sieve_binary_up_to_date(sbin, flags)) {
			/* Not up to date */
			e_debug(svinst->event,
				"Script binary %s is not up-to-date",
				sieve_binary_path(sbin));
			sieve_binary_close(&sbin);
		}
	}

	/* If the binary does not exist or is not up-to-date, we need
	 * to (re-)compile.
	 */
	if (sbin != NULL) {
		e_debug(svinst->event,
			"Script binary %s successfully loaded",
			sieve_binary_path(sbin));
	} else {
		if (sieve_compile_script(script, ehandler, flags,
					 &sbin, error_code_r) < 0)
			return -1;

		e_debug(svinst->event,
			"Script '%s' successfully compiled",
			sieve_script_label(script));

		sieve_binary_set_resource_usage(sbin, &rusage);
	}

	/* Check whether binary can be executed. */
	ret = sieve_binary_check_executable(sbin, error_code_r, &error);
	if (ret <= 0) {
		const char *path = sieve_binary_path(sbin);

		i_assert(error != NULL);
		if (path != NULL) {
			e_debug(svinst->event,
				"Script binary %s cannot be executed",
				path);
		} else {
			e_debug(svinst->event,
				"Script binary from %s cannot be executed",
				sieve_binary_source(sbin));
		}
		if (ret < 0) {
			sieve_internal_error(ehandler,
					     sieve_script_name(script),
					     "failed to open script");
		} else {
			sieve_error(ehandler, sieve_script_name(script),
				    "%s", error);
		}
		sieve_binary_close(&sbin);
		return -1;
	}

	*sbin_r = sbin;
	return 0;
}

int sieve_open_script(struct sieve_script *script,
		      struct sieve_error_handler *ehandler,
		      enum sieve_compile_flags flags,
		      struct sieve_binary **sbin_r,
		      enum sieve_error *error_code_r)
{
	int ret;

	*sbin_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);

	T_BEGIN {
		ret = sieve_open_script_real(script, ehandler, flags,
					     sbin_r, error_code_r);
	} T_END;

	return ret;
}

int sieve_open(struct sieve_instance *svinst, const char *script_location,
	       const char *script_name, struct sieve_error_handler *ehandler,
	       enum sieve_compile_flags flags, struct sieve_binary **sbin_r,
	       enum sieve_error *error_code_r)
{
	struct sieve_script *script;
	bool no_error_result = (error_code_r == NULL);
	int ret;

	*sbin_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);

	/* First open the scriptfile itself */
	if (sieve_script_create_open(svinst, script_location, script_name,
				     &script, error_code_r) < 0) {
		/* Failed */
		switch (*error_code_r) {
		case SIEVE_ERROR_NOT_FOUND:
			if (no_error_result) {
				sieve_error(ehandler, script_name,
					    "script not found");
			}
			break;
		default:
			sieve_internal_error(ehandler, script_name,
					     "failed to open script");
		}
		return -1;
	}

	/* Drop script reference, if sbin != NULL it holds a reference of its
	   own. Otherwise the script object is freed here.
	 */
	ret = sieve_open_script(script, ehandler, flags,
				sbin_r, error_code_r);
	sieve_script_unref(&script);
	return ret;
}

const char *sieve_get_source(struct sieve_binary *sbin)
{
	return sieve_binary_source(sbin);
}

bool sieve_is_loaded(struct sieve_binary *sbin)
{
	return sieve_binary_loaded(sbin);
}

int sieve_save_as(struct sieve_binary *sbin, const char *bin_path, bool update,
		  mode_t save_mode, enum sieve_error *error_code_r)
{
	if (bin_path == NULL)
		return sieve_save(sbin, update, error_code_r);

	return sieve_binary_save(sbin, bin_path, update, save_mode,
				 error_code_r);
}

int sieve_save(struct sieve_binary *sbin, bool update,
	       enum sieve_error *error_code_r)
{
	struct sieve_script *script = sieve_binary_script(sbin);

	if (script == NULL) {
		return sieve_binary_save(sbin, NULL, update, 0600,
					 error_code_r);
	}

	return sieve_script_binary_save(script, sbin, update, error_code_r);
}

bool sieve_record_resource_usage(struct sieve_binary *sbin,
				 const struct sieve_resource_usage *rusage)
{
	return sieve_binary_record_resource_usage(sbin, rusage);
}

int sieve_check_executable(struct sieve_binary *sbin,
			   enum sieve_error *error_code_r,
			   const char **client_error_r)
{
	return sieve_binary_check_executable(sbin, error_code_r,
					     client_error_r);
}

void sieve_close(struct sieve_binary **_sbin)
{
	sieve_binary_close(_sbin);
}

/*
 * Debugging
 */

void sieve_dump(struct sieve_binary *sbin, struct ostream *stream, bool verbose)
{
	struct sieve_binary_dumper *dumpr = sieve_binary_dumper_create(sbin);

	sieve_binary_dumper_run(dumpr, stream, verbose);

	sieve_binary_dumper_free(&dumpr);
}

void sieve_hexdump(struct sieve_binary *sbin, struct ostream *stream)
{
	struct sieve_binary_dumper *dumpr = sieve_binary_dumper_create(sbin);

	sieve_binary_dumper_hexdump(dumpr, stream);

	sieve_binary_dumper_free(&dumpr);
}

int sieve_test(struct sieve_binary *sbin,
	       const struct sieve_message_data *msgdata,
	       const struct sieve_script_env *senv,
	       struct sieve_error_handler *ehandler, struct ostream *stream,
	       enum sieve_execute_flags flags)
{
	struct sieve_instance *svinst = sieve_binary_svinst(sbin);
	struct sieve_result *result;
	struct sieve_execute_env eenv;
	pool_t pool;
	int ret;

	pool = pool_alloconly_create("sieve execution", 4096);
	sieve_execute_init(&eenv, svinst, pool, msgdata, senv, flags);

	/* Create result object */
	result = sieve_result_create(svinst, pool, &eenv);

	/* Run the script */
	ret = sieve_run(sbin, result, &eenv, ehandler);

	/* Print result if successful */
	if (ret > 0) {
		ret = (sieve_result_print(result, senv, stream, NULL) ?
		       SIEVE_EXEC_OK : SIEVE_EXEC_FAILURE);
	}

	/* Cleanup */
	if (result != NULL)
		sieve_result_unref(&result);
	sieve_execute_deinit(&eenv);
	pool_unref(&pool);

	return ret;
}

/*
 * Script execution
 */

int sieve_script_env_init(struct sieve_script_env *senv, struct mail_user *user,
			  const char **error_r)
{
	const struct message_address *postmaster;
	const char *error;

	if (!mail_user_get_postmaster_address(user, &postmaster, &error)) {
		*error_r = t_strdup_printf(
			"Invalid postmaster_address: %s", error);
		return -1;
	}

	i_zero(senv);
	senv->user = user;
	senv->postmaster_address = postmaster;
	return 0;
}

int sieve_execute(struct sieve_binary *sbin,
		  const struct sieve_message_data *msgdata,
		  const struct sieve_script_env *senv,
		  struct sieve_error_handler *exec_ehandler,
		  struct sieve_error_handler *action_ehandler,
		  enum sieve_execute_flags flags)
{
	struct sieve_instance *svinst = sieve_binary_svinst(sbin);
	struct sieve_result *result = NULL;
	struct sieve_result_execution *rexec;
	struct sieve_execute_env eenv;
	pool_t pool;
	int ret;

	pool = pool_alloconly_create("sieve execution", 4096);
	sieve_execute_init(&eenv, svinst, pool, msgdata, senv, flags);

	/* Create result object */
	result = sieve_result_create(svinst, pool, &eenv);

	/* Run the script */
	ret = sieve_run(sbin, result, &eenv, exec_ehandler);

	rexec = sieve_result_execution_create(result, pool);

	/* Evaluate status and execute the result:
	   Strange situations, e.g. currupt binaries, must be handled by the
	   caller. In that case no implicit keep is attempted, because the
	   situation may be resolved.
	 */
	ret = sieve_result_execute(rexec, ret, TRUE, action_ehandler, NULL);

	sieve_result_execution_destroy(&rexec);

	/* Cleanup */
	if (result != NULL)
		sieve_result_unref(&result);
	sieve_execute_finish(&eenv, ret);
	sieve_execute_deinit(&eenv);
	pool_unref(&pool);

	return ret;
}

/*
 * Multiscript support
 */

struct sieve_multiscript {
	pool_t pool;
	struct sieve_execute_env exec_env;
	struct sieve_result *result;
	struct sieve_result_execution *rexec;
	struct event *event;

	int status;
	bool keep;

	struct ostream *teststream;

	bool active:1;
	bool discard_handled:1;
};

struct sieve_multiscript *
sieve_multiscript_start_execute(struct sieve_instance *svinst,
				const struct sieve_message_data *msgdata,
				const struct sieve_script_env *senv)
{
	pool_t pool;
	struct sieve_result *result;
	struct sieve_multiscript *mscript;

	pool = pool_alloconly_create("sieve execution", 4096);
	mscript = p_new(pool, struct sieve_multiscript, 1);
	mscript->pool = pool;
	sieve_execute_init(&mscript->exec_env, svinst, pool, msgdata, senv, 0);

	mscript->event = event_create(mscript->exec_env.event);
	event_set_append_log_prefix(mscript->event, "multi-script: ");

	result = sieve_result_create(svinst, pool, &mscript->exec_env);
	sieve_result_set_keep_action(result, NULL, NULL);
	mscript->result = result;

	mscript->rexec = sieve_result_execution_create(result, pool);

	mscript->status = SIEVE_EXEC_OK;
	mscript->active = TRUE;
	mscript->keep = TRUE;

	e_debug(mscript->event, "Start execute sequence");

	return mscript;
}

static void sieve_multiscript_destroy(struct sieve_multiscript **_mscript)
{
	struct sieve_multiscript *mscript = *_mscript;

	if (mscript == NULL)
		return;
	*_mscript = NULL;

	e_debug(mscript->event, "Destroy");

	event_unref(&mscript->event);

	sieve_result_execution_destroy(&mscript->rexec);
	sieve_result_unref(&mscript->result);
	sieve_execute_deinit(&mscript->exec_env);
	pool_unref(&mscript->pool);
}

struct sieve_multiscript *
sieve_multiscript_start_test(struct sieve_instance *svinst,
			     const struct sieve_message_data *msgdata,
			     const struct sieve_script_env *senv,
			     struct ostream *stream)
{
	struct sieve_multiscript *mscript =
		sieve_multiscript_start_execute(svinst, msgdata, senv);

	mscript->teststream = stream;

	return mscript;
}

static void
sieve_multiscript_test(struct sieve_multiscript *mscript)
{
	const struct sieve_script_env *senv = mscript->exec_env.scriptenv;

	e_debug(mscript->event, "Test result");

	if (mscript->status > 0) {
		mscript->status =
			(sieve_result_print(mscript->result, senv,
					    mscript->teststream,
					    &mscript->keep) ?
			 SIEVE_EXEC_OK : SIEVE_EXEC_FAILURE);
	} else {
		mscript->keep = TRUE;
	}

	sieve_result_mark_executed(mscript->result);
}

static void
sieve_multiscript_execute(struct sieve_multiscript *mscript,
			  struct sieve_error_handler *ehandler,
			  enum sieve_execute_flags flags)
{
	e_debug(mscript->event, "Execute result");

	mscript->exec_env.flags = flags;

	if (mscript->status > 0) {
		mscript->status = sieve_result_execute(mscript->rexec,
						       SIEVE_EXEC_OK, FALSE,
						       ehandler,
						       &mscript->keep);
	}
}

bool sieve_multiscript_run(struct sieve_multiscript *mscript,
			   struct sieve_binary *sbin,
			   struct sieve_error_handler *exec_ehandler,
			   struct sieve_error_handler *action_ehandler,
			   enum sieve_execute_flags flags)
{
	if (!mscript->active) {
		e_debug(mscript->event, "Sequence ended");
		return FALSE;
	}

	e_debug(mscript->event, "Run script '%s'", sieve_binary_source(sbin));

	/* Run the script */
	mscript->exec_env.flags = flags;
	mscript->status = sieve_run(sbin, mscript->result, &mscript->exec_env,
				    exec_ehandler);

	if (mscript->status >= 0) {
		mscript->keep = FALSE;

		if (mscript->teststream != NULL)
			sieve_multiscript_test(mscript);
		else {
			sieve_multiscript_execute(mscript, action_ehandler,
						  flags);
		}
		if (!mscript->keep)
			mscript->active = FALSE;
	}

	if (!mscript->active || mscript->status <= 0) {
		e_debug(mscript->event, "Sequence ended");
		mscript->active = FALSE;
		return FALSE;
	}

	e_debug(mscript->event, "Sequence active");
	return TRUE;
}

bool sieve_multiscript_will_discard(struct sieve_multiscript *mscript)
{
	return (!mscript->active && mscript->status == SIEVE_EXEC_OK &&
		!sieve_result_executed_delivery(mscript->rexec));
}

void sieve_multiscript_run_discard(struct sieve_multiscript *mscript,
				   struct sieve_binary *sbin,
				   struct sieve_error_handler *exec_ehandler,
				   struct sieve_error_handler *action_ehandler,
				   enum sieve_execute_flags flags)
{
	if (!sieve_multiscript_will_discard(mscript)) {
		e_debug(mscript->event, "Not running discard script");
		return;
	}
	i_assert(!mscript->discard_handled);

	e_debug(mscript->event, "Run discard script '%s'",
		sieve_binary_source(sbin));

	sieve_result_set_keep_action(mscript->result, NULL, &act_store);

	/* Run the discard script */
	flags |= SIEVE_EXECUTE_FLAG_DEFER_KEEP;
	mscript->exec_env.flags = flags;
	mscript->status = sieve_run(sbin, mscript->result, &mscript->exec_env,
				    exec_ehandler);

	if (mscript->status >= 0) {
		mscript->keep = FALSE;

		if (mscript->teststream != NULL)
			sieve_multiscript_test(mscript);
		else {
			sieve_multiscript_execute(mscript, action_ehandler,
						  flags);
		}
		if (mscript->status == SIEVE_EXEC_FAILURE)
			mscript->status = SIEVE_EXEC_KEEP_FAILED;
		mscript->active = FALSE;
	}

	mscript->discard_handled = TRUE;
}

int sieve_multiscript_status(struct sieve_multiscript *mscript)
{
	return mscript->status;
}

int sieve_multiscript_finish(struct sieve_multiscript **_mscript,
			     struct sieve_error_handler *action_ehandler,
			     enum sieve_execute_flags flags, int status)
{
	struct sieve_multiscript *mscript = *_mscript;

	if (mscript == NULL)
		return SIEVE_EXEC_OK;
	*_mscript = NULL;

	switch (status) {
	case SIEVE_EXEC_OK:
		status = mscript->status;
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
	case SIEVE_EXEC_FAILURE:
	case SIEVE_EXEC_KEEP_FAILED:
	case SIEVE_EXEC_RESOURCE_LIMIT:
		if (mscript->status == SIEVE_EXEC_TEMP_FAILURE)
			status = mscript->status;
		break;
	}

	e_debug(mscript->event, "Finishing sequence (status=%s)",
		sieve_execution_exitcode_to_str(status));

	mscript->exec_env.flags = flags;
	sieve_result_set_keep_action(mscript->result, NULL, &act_store);

	mscript->keep = FALSE;
	if (mscript->teststream != NULL)
		mscript->keep = TRUE;
	else {
		status = sieve_result_execute(
			mscript->rexec, status, TRUE, action_ehandler,
			&mscript->keep);
	}

	e_debug(mscript->event, "Sequence finished (status=%s, keep=%s)",
		sieve_execution_exitcode_to_str(status),
		(mscript->keep ? "yes" : "no"));

	sieve_execute_finish(&mscript->exec_env, status);

	/* Cleanup */
	sieve_multiscript_destroy(&mscript);

	return status;
}

/*
 * Configured Limits
 */

unsigned int sieve_max_redirects(struct sieve_instance *svinst)
{
	return svinst->set->max_redirects;
}

unsigned int sieve_max_actions(struct sieve_instance *svinst)
{
	return svinst->set->max_actions;
}

size_t sieve_max_script_size(struct sieve_instance *svinst)
{
	return svinst->set->max_script_size;
}

/*
 * Errors
 */

#define CRITICAL_MSG \
	"Internal error occurred. Refer to server log for more information."
#define CRITICAL_MSG_STAMP CRITICAL_MSG " [%Y-%m-%d %H:%M:%S]"

void sieve_error_args_init(enum sieve_error **error_code_r,
			   const char ***error_r)
{
	/* Dummies */
	static enum sieve_error dummy_error_code = SIEVE_ERROR_NONE;
	static const char *dummy_error = NULL;

	if (error_code_r != NULL) {
		if (*error_code_r == NULL)
			*error_code_r = &dummy_error_code;
		**error_code_r = SIEVE_ERROR_NONE;
	}
	if (error_r != NULL) {
		if (*error_r == NULL)
			*error_r = &dummy_error;
		**error_r = NULL;
	}
}

void sieve_error_create_internal(enum sieve_error *error_code_r,
				 const char **error_r)
{
	struct tm *tm;
	char buf[256];

	/* Critical errors may contain sensitive data, so let user see only
	   "Internal error" with a timestamp to make it easier to look from log
	   files the actual error message. */
	tm = localtime(&ioloop_time);

	if (strftime(buf, sizeof(buf), CRITICAL_MSG_STAMP, tm) > 0)
		*error_r = t_strdup(buf);
	else
		*error_r = CRITICAL_MSG;
	*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
}

void sieve_error_create_script_not_found(const char *script_name,
					 enum sieve_error *error_code_r,
					 const char **error_r)
{
	*error_code_r = SIEVE_ERROR_NOT_FOUND;
	if (script_name == NULL)
		*error_r = "Sieve script not found";
	else
		*error_r = t_strdup_printf("Sieve script '%s' not found",
					   script_name);
}

/*
 * User log
 */

const char *
sieve_user_get_log_path(struct sieve_instance *svinst,
			struct sieve_script *user_script)
{
	const char *log_path = (*svinst->set->user_log == '\0' ?
				NULL : svinst->set->user_log);

	/* Determine user log file path */
	if (log_path == NULL) {
		const char *path;

		if (user_script == NULL ||
		    (path = sieve_file_script_get_path(user_script)) == NULL) {
			/* Default */
			if (svinst->home_dir != NULL) {
				log_path = t_strconcat(
					svinst->home_dir, "/.dovecot.sieve.log",
					NULL);
			}
		} else {
			/* Use script file as a base (legacy behavior) */
			log_path = t_strconcat(path, ".log", NULL);
		}
	} else if (svinst->home_dir != NULL) {
		/* Expand home dir if necessary */
		if (log_path[0] == '~') {
			log_path = home_expand_tilde(log_path,
						     svinst->home_dir);
		} else if (log_path[0] != '/') {
			log_path = t_strconcat(svinst->home_dir, "/",
					       log_path, NULL);
		}
	}
	return log_path;
}

/*
 * Script trace log
 */

struct sieve_trace_log {
	struct sieve_instance *svinst;
	struct ostream *output;
};

int sieve_trace_log_create(struct sieve_instance *svinst, const char *path,
			   struct sieve_trace_log **trace_log_r)
{
	struct sieve_trace_log *trace_log;
	struct ostream *output;
	int fd;

	*trace_log_r = NULL;

	if (path == NULL)
		output = o_stream_create_fd(1, 0);
	else {
		fd = open(path, O_CREAT | O_APPEND | O_WRONLY, 0600);
		if (fd == -1) {
			e_error(svinst->event, "trace: "
				"creat(%s) failed: %m", path);
			return -1;
		}
		output = o_stream_create_fd_autoclose(&fd, 0);
		o_stream_set_name(output, path);
	}

	trace_log = i_new(struct sieve_trace_log, 1);
	trace_log->svinst = svinst;
	trace_log->output = output;

	*trace_log_r = trace_log;
	return 0;
}

int sieve_trace_log_create_dir(struct sieve_instance *svinst, const char *dir,
			       struct sieve_trace_log **trace_log_r)
{
	static unsigned int counter = 0;
	const char *timestamp, *prefix;
	struct stat st;

	*trace_log_r = NULL;

	if (stat(dir, &st) < 0) {
		if (errno != ENOENT && errno != EACCES) {
			e_error(svinst->event, "trace: "
				"stat(%s) failed: %m", dir);
		}
		return -1;
	}

	timestamp = t_strflocaltime("%Y%m%d-%H%M%S", ioloop_time);

	counter++;

	prefix = t_strdup_printf("%s/%s.%s.%u.trace",
				 dir, timestamp, my_pid, counter);
	return sieve_trace_log_create(svinst, prefix, trace_log_r);
}

int sieve_trace_log_open(struct sieve_instance *svinst,
			 struct sieve_trace_log **trace_log_r)
{
	const char *trace_dir = svinst->set->trace_dir;

	*trace_log_r = NULL;
	if (*trace_dir == '\0')
		return -1;

	if (svinst->home_dir != NULL) {
		/* Expand home dir if necessary */
		if (trace_dir[0] == '~') {
			trace_dir = home_expand_tilde(trace_dir,
						      svinst->home_dir);
		} else if (trace_dir[0] != '/') {
			trace_dir = t_strconcat(svinst->home_dir, "/",
						trace_dir, NULL);
		}
	}

	return sieve_trace_log_create_dir(svinst, trace_dir, trace_log_r);
}

void sieve_trace_log_write_line(struct sieve_trace_log *trace_log,
				const string_t *line)
{
	struct const_iovec iov[2];

	if (line == NULL) {
		o_stream_nsend_str(trace_log->output, "\n");
		return;
	}

	memset(iov, 0, sizeof(iov));
	iov[0].iov_base = str_data(line);
	iov[0].iov_len = str_len(line);
	iov[1].iov_base = "\n";
	iov[1].iov_len = 1;
	o_stream_nsendv(trace_log->output, iov, 2);
}

void sieve_trace_log_printf(struct sieve_trace_log *trace_log,
			    const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	T_BEGIN {
		o_stream_nsend_str(trace_log->output,
				   t_strdup_vprintf(fmt, args));
	} T_END;
	va_end(args);
}

void sieve_trace_log_free(struct sieve_trace_log **_trace_log)
{
	struct sieve_trace_log *trace_log = *_trace_log;

	*_trace_log = NULL;

	if (o_stream_finish(trace_log->output) < 0) {
		e_error(trace_log->svinst->event, "write(%s) failed: %s",
			o_stream_get_name(trace_log->output),
			o_stream_get_error(trace_log->output));
	}
	o_stream_destroy(&trace_log->output);
	i_free(trace_log);
}

int sieve_trace_config_get(struct sieve_instance *svinst,
			   struct sieve_trace_config *tr_config)
{
	const char *tr_level = svinst->set->trace_level;

	i_zero(tr_config);

	if (*tr_level == '\0' || strcasecmp(tr_level, "none") == 0)
		return -1;

	if (strcasecmp(tr_level, "actions") == 0)
		tr_config->level = SIEVE_TRLVL_ACTIONS;
	else if (strcasecmp(tr_level, "commands") == 0)
		tr_config->level = SIEVE_TRLVL_COMMANDS;
	else if (strcasecmp(tr_level, "tests") == 0)
		tr_config->level = SIEVE_TRLVL_TESTS;
	else if (strcasecmp(tr_level, "matching") == 0)
		tr_config->level = SIEVE_TRLVL_MATCHING;
	else {
		e_error(svinst->event, "Unknown trace level: %s", tr_level);
		return -1;
	}

	if (svinst->set->trace_debug)
		tr_config->flags |= SIEVE_TRFLG_DEBUG;
	if (svinst->set->trace_addresses)
		tr_config->flags |= SIEVE_TRFLG_ADDRESSES;
	return 0;
}

/*
 * Execution exit codes
 */

const char *sieve_execution_exitcode_to_str(int code)
{
	switch (code) {
	case SIEVE_EXEC_OK:
		return "ok";
	case SIEVE_EXEC_FAILURE:
		return "failure";
	case SIEVE_EXEC_TEMP_FAILURE:
		return "temporary_failure";
	case SIEVE_EXEC_BIN_CORRUPT:
		return "binary_corrupt";
	case SIEVE_EXEC_KEEP_FAILED:
		return "keep_failed";
	case SIEVE_EXEC_RESOURCE_LIMIT:
		return "resource_limit";
	}
	i_unreached();
}

/*
 * User e-mail address
 */

const struct smtp_address *sieve_get_user_email(struct sieve_instance *svinst)
{
	struct smtp_address *address;
	const char *username = svinst->username;

	if (svinst->user_email_implicit != NULL)
		return svinst->user_email_implicit;
	if (svinst->set->parsed.user_email != NULL)
		return svinst->set->parsed.user_email;

	if (smtp_address_parse_mailbox(svinst->pool, username, 0,
				       &address, NULL) >= 0) {
		svinst->user_email_implicit = address;
		return svinst->user_email_implicit;
	}

	if (svinst->domainname != NULL) {
		svinst->user_email_implicit = smtp_address_create(
			svinst->pool, username, svinst->domainname);
		return svinst->user_email_implicit;
	}
	return NULL;
}

/*
 * Postmaster address
 */

const struct message_address *
sieve_get_postmaster(const struct sieve_script_env *senv)
{
	i_assert(senv->postmaster_address != NULL);
	return senv->postmaster_address;
}

const struct smtp_address *
sieve_get_postmaster_smtp(const struct sieve_script_env *senv)
{
	struct smtp_address *addr;
	int ret;

	ret = smtp_address_create_from_msg_temp(
		sieve_get_postmaster(senv), &addr);
	i_assert(ret >= 0);
	return addr;
}

const char *sieve_get_postmaster_address(const struct sieve_script_env *senv)
{
	const struct message_address *postmaster =
		sieve_get_postmaster(senv);
	string_t *addr = t_str_new(256);

	message_address_write(addr, postmaster);
	return str_c(addr);
}

/*
 * Resource usage
 */

void sieve_resource_usage_init(struct sieve_resource_usage *rusage_r)
{
	i_zero(rusage_r);
}

void sieve_resource_usage_add(struct sieve_resource_usage *dst,
			      const struct sieve_resource_usage *src)
{
	if ((UINT_MAX - dst->cpu_time_msecs) < src->cpu_time_msecs)
		dst->cpu_time_msecs = UINT_MAX;
	else
		dst->cpu_time_msecs += src->cpu_time_msecs;
}

bool sieve_resource_usage_is_high(struct sieve_instance *svinst ATTR_UNUSED,
				  const struct sieve_resource_usage *rusage)
{
	return (rusage->cpu_time_msecs > SIEVE_HIGH_CPU_TIME_MSECS);
}

bool sieve_resource_usage_is_excessive(
	struct sieve_instance *svinst,
	const struct sieve_resource_usage *rusage)
{
	i_assert(svinst->set->max_cpu_time <= (UINT_MAX / 1000));
	if (svinst->set->max_cpu_time == 0)
		return FALSE;
	return (rusage->cpu_time_msecs >
		(svinst->set->max_cpu_time * 1000));
}

const char *
sieve_resource_usage_get_summary(const struct sieve_resource_usage *rusage)
{
	if (rusage->cpu_time_msecs == 0)
		return "no usage recorded";

	return t_strdup_printf("cpu time = %u ms", rusage->cpu_time_msecs);
}
