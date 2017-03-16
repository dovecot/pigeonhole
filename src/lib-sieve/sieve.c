/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
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

#include "sieve-settings.h"
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
#include "sieve-error-private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>

/*
 * Main Sieve library interface
 */

struct sieve_instance *sieve_init
(const struct sieve_environment *env,
	const struct sieve_callbacks *callbacks, void *context, bool debug)
{
	struct sieve_instance *svinst;
	const char *domain;
	pool_t pool;

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

	/* Determine domain */
	if ( env->domainname != NULL && *(env->domainname) != '\0' ) {
		domain = env->domainname;
	} else {
		/* Fall back to parsing username localpart@domain */
		domain = svinst->username == NULL ? NULL :
			strchr(svinst->username, '@');
		if ( domain == NULL || *(domain+1) == '\0' ) {
			/* Fall back to parsing hostname host.domain */
			domain = ( env->hostname != NULL ? strchr(env->hostname, '.') : NULL );
			if ( domain == NULL || *(domain+1) == '\0'
				|| strchr(domain+1, '.') == NULL ) {
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

	if ( debug ) {
		sieve_sys_debug(svinst, "%s version %s initializing",
			PIGEONHOLE_NAME, PIGEONHOLE_VERSION_FULL);
	}

	/* Read configuration */

	sieve_settings_load(svinst);

	/* Initialize extensions */
	if ( !sieve_extensions_init(svinst) ) {
		sieve_deinit(&svinst);
		return NULL;
	}

	/* Initialize storage classes */
	sieve_storages_init(svinst);

	/* Initialize plugins */
	sieve_plugins_load(svinst, NULL, NULL);

	/* Configure extensions */
	sieve_extensions_configure(svinst);

	return svinst;
}

void sieve_deinit(struct sieve_instance **_svinst)
{
	struct sieve_instance *svinst = *_svinst;

	sieve_plugins_unload(svinst);
	sieve_storages_deinit(svinst);
	sieve_extensions_deinit(svinst);
	sieve_errors_deinit(svinst);

	pool_unref(&(svinst)->pool);
	*_svinst = NULL;
}

void sieve_set_extensions
(struct sieve_instance *svinst, const char *extensions)
{
	sieve_extensions_set_string(svinst, extensions, FALSE, FALSE);
}

const char *sieve_get_capabilities
(struct sieve_instance *svinst, const char *name)
{
	if ( name == NULL || *name == '\0' )
		return sieve_extensions_get_string(svinst);

	return sieve_extension_capabilities_get_string(svinst, name);
}

/*
 * Low-level compiler functions
 */

struct sieve_ast *sieve_parse
(struct sieve_script *script, struct sieve_error_handler *ehandler,
	enum sieve_error *error_r)
{
	struct sieve_parser *parser;
	struct sieve_ast *ast = NULL;

	/* Parse */
	if ( (parser = sieve_parser_create(script, ehandler, error_r)) == NULL )
		return NULL;

 	if ( !sieve_parser_run(parser, &ast) ) {
 		ast = NULL;
 	} else
		sieve_ast_ref(ast);

	sieve_parser_free(&parser);

	if ( error_r != NULL ) {
		if ( ast == NULL )
			*error_r = SIEVE_ERROR_NOT_VALID;
		else
			*error_r = SIEVE_ERROR_NONE;
	}

	return ast;
}

bool sieve_validate
(struct sieve_ast *ast, struct sieve_error_handler *ehandler,
	enum sieve_compile_flags flags, enum sieve_error *error_r)
{
	bool result = TRUE;
	struct sieve_validator *validator =
		sieve_validator_create(ast, ehandler, flags);

	if ( !sieve_validator_run(validator) )
		result = FALSE;

	sieve_validator_free(&validator);

	if ( error_r != NULL ) {
		if ( !result )
			*error_r = SIEVE_ERROR_NOT_VALID;
		else
			*error_r = SIEVE_ERROR_NONE;
	}

	return result;
}

static struct sieve_binary *sieve_generate
(struct sieve_ast *ast, struct sieve_error_handler *ehandler,
	enum sieve_compile_flags flags, enum sieve_error *error_r)
{
	struct sieve_generator *generator =
		sieve_generator_create(ast, ehandler, flags);
	struct sieve_binary *sbin = NULL;

	sbin = sieve_generator_run(generator, NULL);

	sieve_generator_free(&generator);

	if ( error_r != NULL ) {
		if ( sbin == NULL )
			*error_r = SIEVE_ERROR_NOT_VALID;
		else
			*error_r = SIEVE_ERROR_NONE;
	}

	return sbin;
}

/*
 * Sieve compilation
 */

struct sieve_binary *sieve_compile_script
(struct sieve_script *script, struct sieve_error_handler *ehandler,
	enum sieve_compile_flags flags, enum sieve_error *error_r)
{
	struct sieve_ast *ast;
	struct sieve_binary *sbin;
	enum sieve_error error, *errorp;

	if ( error_r != NULL )
		errorp = error_r;
	else
		errorp = &error;
	*errorp = SIEVE_ERROR_NONE;

	/* Parse */
	if ( (ast = sieve_parse(script, ehandler, errorp)) == NULL ) {
		switch ( *errorp ) {
		case SIEVE_ERROR_NOT_FOUND:
			if (error_r == NULL) {
				sieve_error(ehandler, sieve_script_name(script),
					"script not found");
			}
			break;
		default:
			sieve_error(ehandler, sieve_script_name(script),
				"parse failed");
		}
		return NULL;
	}

	/* Validate */
	if ( !sieve_validate(ast, ehandler, flags, errorp) ) {
		sieve_error(ehandler, sieve_script_name(script),
			"validation failed");

 		sieve_ast_unref(&ast);
 		return NULL;
 	}

	/* Generate */
	if ( (sbin=sieve_generate(ast, ehandler, flags, errorp)) == NULL ) {
		sieve_error(ehandler, sieve_script_name(script),
			"code generation failed");
		sieve_ast_unref(&ast);
		return NULL;
	}

	/* Cleanup */
	sieve_ast_unref(&ast);
	return sbin;
}

struct sieve_binary *sieve_compile
(struct sieve_instance *svinst, const char *script_location,
	const char *script_name, struct sieve_error_handler *ehandler,
	enum sieve_compile_flags flags, enum sieve_error *error_r)
{
	struct sieve_script *script;
	struct sieve_binary *sbin;
	enum sieve_error error;

	if ( (script = sieve_script_create_open
		(svinst, script_location, script_name, &error)) == NULL ) {
		if (error_r != NULL)
			*error_r = error;
		switch ( error ) {
		case SIEVE_ERROR_NOT_FOUND:
			sieve_error(ehandler, script_name, "script not found");
			break;
		default:
			sieve_internal_error(ehandler, script_name, "failed to open script");
		}
		return NULL;
	}

	sbin = sieve_compile_script(script, ehandler, flags, error_r);

	if ( svinst->debug && sbin != NULL ) {
		sieve_sys_debug(svinst, "Script `%s' from %s successfully compiled",
			sieve_script_name(script), sieve_script_location(script));
	}

	sieve_script_unref(&script);

	return sbin;
}

/*
 * Sieve runtime
 */

static int sieve_run
(struct sieve_binary *sbin, struct sieve_result **result,
	const struct sieve_message_data *msgdata, const struct sieve_script_env *senv,
	struct sieve_error_handler *ehandler, enum sieve_execute_flags flags)
{
	struct sieve_interpreter *interp;
	int ret = 0;

	/* Create the interpreter */
	if ( (interp=sieve_interpreter_create
		(sbin, NULL, msgdata, senv, ehandler, flags)) == NULL )
		return SIEVE_EXEC_BIN_CORRUPT;

	/* Reset execution status */
	if ( senv->exec_status != NULL )
		i_zero(senv->exec_status);

	/* Create result object */
	if ( *result == NULL ) {
		*result = sieve_result_create
			(sieve_binary_svinst(sbin), msgdata, senv);
	}

	/* Run the interpreter */
	ret = sieve_interpreter_run(interp, *result);

	/* Free the interpreter */
	sieve_interpreter_free(&interp);

	return ret;
}

/*
 * Reading/writing sieve binaries
 */

struct sieve_binary *sieve_load
(struct sieve_instance *svinst, const char *bin_path, enum sieve_error *error_r)
{
	return sieve_binary_open(svinst, bin_path, NULL, error_r);
}

struct sieve_binary *sieve_open_script
(struct sieve_script *script, struct sieve_error_handler *ehandler,
	enum sieve_compile_flags flags, enum sieve_error *error_r)
{
	struct sieve_instance *svinst = sieve_script_svinst(script);
	struct sieve_binary *sbin;

	T_BEGIN {
		/* Then try to open the matching binary */
		sbin = sieve_script_binary_load(script, error_r);

		if (sbin != NULL) {
			/* Ok, it exists; now let's see if it is up to date */
			if ( !sieve_binary_up_to_date(sbin, flags) ) {
				/* Not up to date */
				if ( svinst->debug ) {
					sieve_sys_debug(svinst, "Script binary %s is not up-to-date",
						sieve_binary_path(sbin));
				}

				sieve_binary_unref(&sbin);
				sbin = NULL;
			}
		}

		/* If the binary does not exist or is not up-to-date, we need
		 * to (re-)compile.
		 */
		if ( sbin != NULL ) {
			if ( svinst->debug ) {
				sieve_sys_debug(svinst,
					"Script binary %s successfully loaded",
					sieve_binary_path(sbin));
			}

		} else {
			sbin = sieve_compile_script(script, ehandler, flags, error_r);

			if ( sbin != NULL ) {
				if ( svinst->debug ) {
					sieve_sys_debug(svinst,
						"Script `%s' from %s successfully compiled",
						sieve_script_name(script), sieve_script_location(script));
				}
			}
		}
	} T_END;

	return sbin;
}

struct sieve_binary *sieve_open
(struct sieve_instance *svinst, const char *script_location,
	const char *script_name, struct sieve_error_handler *ehandler,
	enum sieve_compile_flags flags, enum sieve_error *error_r)
{
	struct sieve_script *script;
	struct sieve_binary *sbin;
	enum sieve_error error;

	/* First open the scriptfile itself */
	if ( (script=sieve_script_create_open
		(svinst, script_location, script_name, &error)) == NULL ) {
		/* Failed */
		if (error_r != NULL)
			*error_r = error;
		switch ( error ) {
		case SIEVE_ERROR_NOT_FOUND:
			sieve_error(ehandler, script_name, "script not found");
			break;
		default:
			sieve_internal_error(ehandler, script_name, "failed to open script");
		}
		return NULL;
	}

	sbin = sieve_open_script(script, ehandler, flags, error_r);

	/* Drop script reference, if sbin != NULL it holds a reference of its own.
	 * Otherwise the script object is freed here.
	 */
	sieve_script_unref(&script);

	return sbin;
}

const char *sieve_get_source(struct sieve_binary *sbin)
{
	return sieve_binary_source(sbin);
}

bool sieve_is_loaded(struct sieve_binary *sbin)
{
	return sieve_binary_loaded(sbin);
}

int sieve_save_as
(struct sieve_binary *sbin, const char *bin_path, bool update,
	mode_t save_mode, enum sieve_error *error_r)
{
	if  ( bin_path == NULL )
		return sieve_save(sbin, update, error_r);

	return sieve_binary_save(sbin, bin_path, update, save_mode, error_r);
}

int sieve_save
(struct sieve_binary *sbin, bool update, enum sieve_error *error_r)
{
	struct sieve_script *script = sieve_binary_script(sbin);

	if ( script == NULL ) {
		return sieve_binary_save(sbin, NULL, update, 0600, error_r);
	}

	return sieve_script_binary_save(script, sbin, update, error_r);
}

void sieve_close(struct sieve_binary **sbin)
{
	sieve_binary_unref(sbin);
}

/*
 * Debugging
 */

void sieve_dump
(struct sieve_binary *sbin, struct ostream *stream, bool verbose)
{
	struct sieve_binary_dumper *dumpr = sieve_binary_dumper_create(sbin);

	sieve_binary_dumper_run(dumpr, stream, verbose);

	sieve_binary_dumper_free(&dumpr);
}

void sieve_hexdump
(struct sieve_binary *sbin, struct ostream *stream)
{
	struct sieve_binary_dumper *dumpr = sieve_binary_dumper_create(sbin);

	sieve_binary_dumper_hexdump(dumpr, stream);

	sieve_binary_dumper_free(&dumpr);
}

int sieve_test
(struct sieve_binary *sbin, const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv, struct sieve_error_handler *ehandler,
	struct ostream *stream, enum sieve_execute_flags flags, bool *keep)
{
	struct sieve_result *result = NULL;
	int ret;

	if ( keep != NULL ) *keep = FALSE;

	/* Run the script */
	ret = sieve_run(sbin, &result, msgdata, senv, ehandler, flags);

	/* Print result if successful */
	if ( ret > 0 ) {
		ret = ( sieve_result_print(result, senv, stream, keep) ?
			SIEVE_EXEC_OK : SIEVE_EXEC_FAILURE );
	} else if ( ret == 0 ) {
		if ( keep != NULL ) *keep = TRUE;
	}

	/* Cleanup */
	if ( result != NULL )
		sieve_result_unref(&result);

	return ret;
}

/*
 * Script execution
 */

int sieve_execute
(struct sieve_binary *sbin, const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv,
	struct sieve_error_handler *exec_ehandler,
	struct sieve_error_handler *action_ehandler,
	enum sieve_execute_flags flags, bool *keep)
{
	struct sieve_result *result = NULL;
	int ret;

	if ( keep != NULL ) *keep = FALSE;

	/* Run the script */
	ret = sieve_run(sbin, &result, msgdata, senv, exec_ehandler, flags);

	/* Evaluate status and execute the result:
	 *   Strange situations, e.g. currupt binaries, must be handled by the caller.
	 *   In that case no implicit keep is attempted, because the situation may be
	 *   resolved.
	 */
	if ( ret > 0 ) {
		/* Execute result */
		ret = sieve_result_execute(result, keep, action_ehandler, flags);
	} else if ( ret == SIEVE_EXEC_FAILURE ) {
		/* Perform implicit keep if script failed with a normal runtime error */
		switch ( sieve_result_implicit_keep
			(result, action_ehandler, flags, FALSE) ) {
		case SIEVE_EXEC_OK:
			if ( keep != NULL ) *keep = TRUE;
			break;
		case SIEVE_EXEC_TEMP_FAILURE:
			ret = SIEVE_EXEC_TEMP_FAILURE;
			break;
		default:
			ret = SIEVE_EXEC_KEEP_FAILED;
		}
	}

	/* Cleanup */
	if ( result != NULL )
		sieve_result_unref(&result);

	return ret;
}

/*
 * Multiscript support
 */

struct sieve_multiscript {
	struct sieve_instance *svinst;
	struct sieve_result *result;
	const struct sieve_message_data *msgdata;
	const struct sieve_script_env *scriptenv;

	int status;
	bool keep;

	struct ostream *teststream;

	bool active:1;
	bool discard_handled:1;
};

struct sieve_multiscript *sieve_multiscript_start_execute
(struct sieve_instance *svinst,	const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv)
{
	pool_t pool;
	struct sieve_result *result;
	struct sieve_multiscript *mscript;

	result = sieve_result_create(svinst, msgdata, senv);
	pool = sieve_result_pool(result);

	sieve_result_set_keep_action(result, NULL, NULL);

	mscript = p_new(pool, struct sieve_multiscript, 1);
	mscript->svinst = svinst;
	mscript->result = result;
	mscript->msgdata = msgdata;
	mscript->scriptenv = senv;
	mscript->status = SIEVE_EXEC_OK;
	mscript->active = TRUE;
	mscript->keep = TRUE;

	return mscript;
}

struct sieve_multiscript *sieve_multiscript_start_test
(struct sieve_instance *svinst, const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv, struct ostream *stream)
{
	struct sieve_multiscript *mscript =
		sieve_multiscript_start_execute(svinst, msgdata, senv);

	mscript->teststream = stream;

	return mscript;
}

static void sieve_multiscript_test
(struct sieve_multiscript *mscript, bool *keep)
{
	if ( mscript->status > 0 ) {
		mscript->status = ( sieve_result_print(mscript->result,
			mscript->scriptenv, mscript->teststream, keep) ?
				SIEVE_EXEC_OK : SIEVE_EXEC_FAILURE );
	} else {
		if ( keep != NULL ) *keep = TRUE;
	}

	sieve_result_mark_executed(mscript->result);
}

static void sieve_multiscript_execute
(struct sieve_multiscript *mscript,
	struct sieve_error_handler *ehandler,
	enum sieve_execute_flags flags, bool *keep)
{
	if ( mscript->status > 0 ) {
		mscript->status = sieve_result_execute
			(mscript->result, keep, ehandler, flags);
	} else {
		if ( sieve_result_implicit_keep
			(mscript->result, ehandler, flags, FALSE) <= 0 )
			mscript->status = SIEVE_EXEC_KEEP_FAILED;
		else
			if ( keep != NULL ) *keep = TRUE;
	}
}

bool sieve_multiscript_run
(struct sieve_multiscript *mscript, struct sieve_binary *sbin,
	struct sieve_error_handler *exec_ehandler,
	struct sieve_error_handler *action_ehandler,
	enum sieve_execute_flags flags)
{
	if ( !mscript->active ) return FALSE;

	/* Run the script */
	mscript->status = sieve_run(sbin, &mscript->result, mscript->msgdata,
		mscript->scriptenv, exec_ehandler, flags);

	if ( mscript->status >= 0 ) {
		mscript->keep = FALSE;

		if ( mscript->teststream != NULL ) {
			sieve_multiscript_test(mscript, &mscript->keep);
		} else {
			sieve_multiscript_execute(mscript,
				action_ehandler, flags, &mscript->keep);
		}
		mscript->active =
			( mscript->active && mscript->keep && mscript->status > 0 );
	}

	if ( mscript->status <= 0 )
		return FALSE;

	return mscript->active;
}

bool sieve_multiscript_will_discard
(struct sieve_multiscript *mscript)
{
	return ( !mscript->active &&
		!sieve_result_executed_delivery(mscript->result) );
}

void sieve_multiscript_run_discard
(struct sieve_multiscript *mscript, struct sieve_binary *sbin,
	struct sieve_error_handler *exec_ehandler,
	struct sieve_error_handler *action_ehandler,
	enum sieve_execute_flags flags)
{
	if ( !sieve_multiscript_will_discard(mscript) )
		return;
	i_assert( !mscript->discard_handled );

	sieve_result_set_keep_action
		(mscript->result, NULL, &act_store);

	/* Run the discard script */
	flags |= SIEVE_EXECUTE_FLAG_DEFER_KEEP;
	mscript->status = sieve_run(sbin, &mscript->result, mscript->msgdata,
		mscript->scriptenv, exec_ehandler, flags);

	if ( mscript->status >= 0 ) {
		mscript->keep = FALSE;

		if ( mscript->teststream != NULL ) {
			sieve_multiscript_test(mscript, &mscript->keep);
		} else {
			sieve_multiscript_execute(mscript,
				action_ehandler, flags, &mscript->keep);
		}
	}

	mscript->discard_handled = TRUE;
}

int sieve_multiscript_status(struct sieve_multiscript *mscript)
{
	return mscript->status;
}

int sieve_multiscript_tempfail(struct sieve_multiscript **_mscript,
	struct sieve_error_handler *action_ehandler,
	enum sieve_execute_flags flags)
{
	struct sieve_multiscript *mscript = *_mscript;
	struct sieve_result *result = mscript->result;
	int ret = mscript->status;

	sieve_result_set_keep_action
		(mscript->result, NULL, &act_store);

	if ( mscript->active ) {
		ret = SIEVE_EXEC_TEMP_FAILURE;

		if ( mscript->teststream == NULL && sieve_result_executed(result) ) {
			/* Part of the result is already executed, need to fall back to
			 * to implicit keep (FIXME)
			 */
			switch ( sieve_result_implicit_keep
				(result, action_ehandler, flags, FALSE) ) {
			case SIEVE_EXEC_OK:
				ret = SIEVE_EXEC_FAILURE;
				break;
			default:
				ret = SIEVE_EXEC_KEEP_FAILED;
			}
		}
	}

	/* Cleanup */
	sieve_result_unref(&result);
	*_mscript = NULL;

	return ret;
}

int sieve_multiscript_finish(struct sieve_multiscript **_mscript,
	struct sieve_error_handler *action_ehandler,
	enum sieve_execute_flags flags, bool *keep)
{
	struct sieve_multiscript *mscript = *_mscript;
	struct sieve_result *result = mscript->result;
	int ret = mscript->status;

	sieve_result_set_keep_action
		(mscript->result, NULL, &act_store);

	if ( mscript->active ) {
		if ( mscript->teststream != NULL ) {
			mscript->keep = TRUE;
		} else {
			switch ( sieve_result_implicit_keep
				(result, action_ehandler, flags, TRUE) ) {
			case SIEVE_EXEC_OK:
				mscript->keep = TRUE;
				break;
			case SIEVE_EXEC_TEMP_FAILURE:
				if (!sieve_result_executed(result)) {
					ret = SIEVE_EXEC_TEMP_FAILURE;
					break;
				}
				/* fall through */
			default:
				ret = SIEVE_EXEC_KEEP_FAILED;
			}
		}
	}

	if ( keep != NULL ) *keep = mscript->keep;

	/* Cleanup */
	sieve_result_unref(&result);
	*_mscript = NULL;
	return ret;
}

/*
 * Configured Limits
 */

unsigned int sieve_max_redirects(struct sieve_instance *svinst)
{
	return svinst->max_redirects;
}

unsigned int sieve_max_actions(struct sieve_instance *svinst)
{
	return svinst->max_actions;
}

size_t sieve_max_script_size(struct sieve_instance *svinst)
{
	return svinst->max_script_size;
}

/*
 * User log
 */

const char *sieve_user_get_log_path
(struct sieve_instance *svinst,
	struct sieve_script *user_script)
{
	const char *log_path = NULL;

	/* Determine user log file path */
	if ( (log_path=sieve_setting_get
		(svinst, "sieve_user_log")) == NULL ) {
		const char *path;

		if ( user_script == NULL ||
			(path=sieve_file_script_get_path(user_script)) == NULL ) {
			/* Default */
			if ( svinst->home_dir != NULL ) {
				log_path = t_strconcat
					(svinst->home_dir, "/.dovecot.sieve.log", NULL);
			}
		} else {
			/* Use script file as a base (legacy behavior) */
			log_path = t_strconcat(path, ".log", NULL);
		}
	} else if ( svinst->home_dir != NULL ) {
		/* Expand home dir if necessary */
		if ( log_path[0] == '~' ) {
			log_path = home_expand_tilde(log_path, svinst->home_dir);
		} else if ( log_path[0] != '/' ) {
			log_path = t_strconcat(svinst->home_dir, "/", log_path, NULL);
		}
	}
	return log_path;
}

/*
 * Script trace log
 */

struct sieve_trace_log {
	struct ostream *output;
};

int sieve_trace_log_create
(struct sieve_instance *svinst, const char *path,
	struct sieve_trace_log **trace_log_r)
{
	struct sieve_trace_log *trace_log;
	struct ostream *output;
	int fd;

	*trace_log_r = NULL;

	if ( path == NULL ) {
		output = o_stream_create_fd(1, 0, FALSE);
	} else {
		fd = open(path, O_CREAT | O_APPEND | O_WRONLY, 0600);
		if ( fd == -1 ) {
			sieve_sys_error(svinst, "trace: "
				"creat(%s) failed: %m", path);
			return -1;
		}
		output = o_stream_create_fd_autoclose(&fd, 0);
	}

	trace_log = i_new(struct sieve_trace_log, 1);
	trace_log->output = output;

	*trace_log_r = trace_log;
	return 0;
}

int sieve_trace_log_create_dir
(struct sieve_instance *svinst, const char *dir,
	const char *label, struct sieve_trace_log **trace_log_r)
{
	static unsigned int counter = 0;
	const char *timestamp, *prefix;
	struct stat st;

	*trace_log_r = NULL;

	if (stat(dir, &st) < 0) {
		if (errno != ENOENT && errno != EACCES) {
			sieve_sys_error(svinst, "trace: "
				"stat(%s) failed: %m", dir);
		}
		return -1;
	}

	timestamp = t_strflocaltime("%Y%m%d-%H%M%S", ioloop_time);

	counter++;
	if ( label != NULL ) {
		prefix = t_strdup_printf("%s/%s.%s.%s.%u.trace",
			dir, label, timestamp, my_pid, counter);
	} else {
		prefix = t_strdup_printf("%s/%s.%s.%u.trace",
			dir, timestamp, my_pid, counter);
	}
	return sieve_trace_log_create(svinst, prefix, trace_log_r);
}

int sieve_trace_log_open
(struct sieve_instance *svinst, const char *label,
	struct sieve_trace_log **trace_log_r)
{
	const char *trace_dir =
		sieve_setting_get(svinst, "sieve_trace_dir");

	*trace_log_r = NULL;
	if (trace_dir == NULL)
		return -1;

	if ( svinst->home_dir != NULL ) {
		/* Expand home dir if necessary */
		if ( trace_dir[0] == '~' ) {
			trace_dir = home_expand_tilde(trace_dir, svinst->home_dir);
		} else if ( trace_dir[0] != '/' ) {
			trace_dir = t_strconcat(svinst->home_dir, "/", trace_dir, NULL);
		}
	}

	return sieve_trace_log_create_dir
		(svinst, trace_dir, label, trace_log_r)	;
}

void sieve_trace_log_write_line
(struct sieve_trace_log *trace_log, const string_t *line)
{
	struct const_iovec iov[2];

	if (line == NULL) {
		o_stream_send_str(trace_log->output, "\n");
		return;
	}

	memset(iov, 0, sizeof(iov));
	iov[0].iov_base = str_data(line);
	iov[0].iov_len = str_len(line);
	iov[1].iov_base = "\n";
	iov[1].iov_len = 1;
	o_stream_sendv(trace_log->output, iov, 2);
}

void sieve_trace_log_free(struct sieve_trace_log **_trace_log)
{
	struct sieve_trace_log *trace_log = *_trace_log;

	*_trace_log = NULL;

	o_stream_destroy(&trace_log->output);
	i_free(trace_log);
}

int sieve_trace_config_get(struct sieve_instance *svinst,
	struct sieve_trace_config *tr_config)
{
	const char *tr_level =
		sieve_setting_get(svinst, "sieve_trace_level");
	bool tr_debug, tr_addresses;

	i_zero(tr_config);

	if ( tr_level == NULL || *tr_level == '\0' ||
		strcasecmp(tr_level, "none") == 0 )
		return -1;

	if ( strcasecmp(tr_level, "actions") == 0 ) {
		tr_config->level = SIEVE_TRLVL_ACTIONS;
	} else if ( strcasecmp(tr_level, "commands") == 0 ) {
		tr_config->level = SIEVE_TRLVL_COMMANDS;
	} else if ( strcasecmp(tr_level, "tests") == 0 ) {
		tr_config->level = SIEVE_TRLVL_TESTS;
	} else if ( strcasecmp(tr_level, "matching") == 0 ) {
		tr_config->level = SIEVE_TRLVL_MATCHING;
	} else {
		sieve_sys_error(svinst,
			"Unknown trace level: %s", tr_level);
		return -1;
	}

	tr_debug = FALSE;
	(void)sieve_setting_get_bool_value
		(svinst, "sieve_trace_debug", &tr_debug);
	tr_addresses = FALSE;
	(void)sieve_setting_get_bool_value
		(svinst, "sieve_trace_addresses", &tr_addresses);

	if (tr_debug)
		tr_config->flags |= SIEVE_TRFLG_DEBUG;
	if (tr_addresses)
		tr_config->flags |= SIEVE_TRFLG_ADDRESSES;
	return 0;
}

/*
 * User e-mail address
 */

const char *sieve_get_user_email
(struct sieve_instance *svinst)
{
	const char *username = svinst->username;

	if (svinst->user_email != NULL)
		return sieve_address_to_string(svinst->user_email);

	if ( strchr(username, '@') != 0 )
		return username;
	if ( svinst->domainname != NULL ) {
		struct sieve_address svaddr;

		i_zero(&svaddr);
		svaddr.local_part = username;
		svaddr.domain = svinst->domainname;
		return sieve_address_to_string(&svaddr);
	}
	return NULL;
}
