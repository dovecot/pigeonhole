/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "istream.h"
#include "buffer.h"
#include "eacces-error.h"
#include "home-expand.h"

#include "sieve-limits.h"
#include "sieve-settings.h"
#include "sieve-extensions.h"
#include "sieve-plugins.h"

#include "sieve-script.h"
#include "sieve-script-file.h"
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
#include <stdlib.h>
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
	unsigned long long int uint_setting;
	size_t size_setting;
	const char  *domain;
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
			PIGEONHOLE_NAME, PIGEONHOLE_VERSION);
	}

	/* Read limits from configuration */

	svinst->max_script_size = SIEVE_DEFAULT_MAX_SCRIPT_SIZE;
	svinst->max_actions = SIEVE_DEFAULT_MAX_ACTIONS;
	svinst->max_redirects = SIEVE_DEFAULT_MAX_REDIRECTS;

	if ( sieve_setting_get_size_value
		(svinst, "sieve_max_script_size", &size_setting) ) {
		svinst->max_script_size = size_setting;
	}

	if ( sieve_setting_get_uint_value
		(svinst, "sieve_max_actions", &uint_setting) ) {
		svinst->max_actions = (unsigned int) uint_setting;
	}

	if ( sieve_setting_get_uint_value
		(svinst, "sieve_max_redirects", &uint_setting) ) {
		svinst->max_redirects = (unsigned int) uint_setting;
	}

	/* Initialize extensions */
	if ( !sieve_extensions_init(svinst) ) {
		sieve_deinit(&svinst);
		return NULL;
	}

	sieve_plugins_load(svinst, NULL, NULL);

	sieve_extensions_configure(svinst);

	return svinst;
}

void sieve_deinit(struct sieve_instance **svinst)
{
	sieve_plugins_unload(*svinst);

	sieve_extensions_deinit(*svinst);

	sieve_errors_deinit(*svinst);

	pool_unref(&(*svinst)->pool);

	*svinst = NULL;
}

void sieve_set_extensions
(struct sieve_instance *svinst, const char *extensions)
{
	sieve_extensions_set_string(svinst, extensions, FALSE);
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

	/* Parse */
	if ( (ast = sieve_parse(script, ehandler, error_r)) == NULL ) {
 		sieve_error(ehandler, sieve_script_name(script), "parse failed");
		return NULL;
	}

	/* Validate */
	if ( !sieve_validate(ast, ehandler, flags, error_r) ) {
		sieve_error(ehandler, sieve_script_name(script), "validation failed");

 		sieve_ast_unref(&ast);
 		return NULL;
 	}

	/* Generate */
	if ( (sbin=sieve_generate(ast, ehandler, flags, error_r)) == NULL ) {
		sieve_error(ehandler, sieve_script_name(script), "code generation failed");

		sieve_ast_unref(&ast);
		return NULL;
	}

	/* Cleanup */
	sieve_ast_unref(&ast);

	if ( error_r != NULL )
		error_r = SIEVE_ERROR_NONE;

	return sbin;
}

struct sieve_binary *sieve_compile
(struct sieve_instance *svinst, const char *script_location,
	const char *script_name, struct sieve_error_handler *ehandler,
	enum sieve_compile_flags flags, enum sieve_error *error_r)
{
	struct sieve_script *script;
	struct sieve_binary *sbin;

	if ( (script = sieve_script_create_open
		(svinst, script_location, script_name, ehandler, error_r)) == NULL )
		return NULL;

	sbin = sieve_compile_script(script, ehandler, flags, error_r);

	if ( svinst->debug && sbin != NULL ) {
		sieve_sys_debug(svinst, "script `%s' from %s successfully compiled",
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
	struct sieve_error_handler *ehandler, enum sieve_runtime_flags flags)
{
	struct sieve_interpreter *interp;
	int ret = 0;

	/* Create the interpreter */
	if ( (interp=sieve_interpreter_create(sbin, msgdata, senv, ehandler, flags))
		== NULL )
		return SIEVE_EXEC_BIN_CORRUPT;

	/* Reset execution status */
	if ( senv->exec_status != NULL )
		memset(senv->exec_status, 0, sizeof(*senv->exec_status));

	/* Create result object */
	if ( *result == NULL )
		*result = sieve_result_create
			(sieve_binary_svinst(sbin), msgdata, senv, ehandler);
	else {
		sieve_result_set_error_handler(*result, ehandler);
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
				if ( svinst->debug )
					sieve_sys_debug(svinst, "script binary %s is not up-to-date",
						sieve_binary_path(sbin));

				sieve_binary_unref(&sbin);
				sbin = NULL;
			}
		}

		/* If the binary does not exist or is not up-to-date, we need
		 * to (re-)compile.
		 */
		if ( sbin != NULL ) {
			if ( svinst->debug )
				sieve_sys_debug(svinst, "script binary %s successfully loaded",
					sieve_binary_path(sbin));

		} else {
			sbin = sieve_compile_script(script, ehandler, flags, error_r);

			if ( sbin != NULL ) {
				if ( svinst->debug )
					sieve_sys_debug(svinst, "script `%s' from %s successfully compiled",
						sieve_script_name(script), sieve_script_location(script));
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

	/* First open the scriptfile itself */
	if ( (script=sieve_script_create_open
		(svinst, script_location, script_name, ehandler, error_r)) == NULL ) {
		/* Failed */
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
	struct ostream *stream, enum sieve_runtime_flags flags, bool *keep)
{
	struct sieve_result *result = NULL;
	int ret;

	if ( keep != NULL ) *keep = FALSE;

	/* Run the script */
	ret = sieve_run(sbin, &result, msgdata, senv, ehandler, flags);

	/* Print result if successful */
	if ( ret > 0 ) {
		ret = sieve_result_print(result, senv, stream, keep);
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
	const struct sieve_script_env *senv, struct sieve_error_handler *ehandler,
	enum sieve_runtime_flags flags, bool *keep)
{
	struct sieve_result *result = NULL;
	int ret;

	if ( keep != NULL ) *keep = FALSE;

	/* Run the script */
	ret = sieve_run(sbin, &result, msgdata, senv, ehandler, flags);

	/* Evaluate status and execute the result:
	 *   Strange situations, e.g. currupt binaries, must be handled by the caller.
	 *   In that case no implicit keep is attempted, because the situation may be
	 *   resolved.
	 */
	if ( ret > 0 ) {
		/* Execute result */
		ret = sieve_result_execute(result, keep);
	} else if ( ret == SIEVE_EXEC_FAILURE ) {
		/* Perform implicit keep if script failed with a normal runtime error */
		switch ( sieve_result_implicit_keep(result) ) {
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
	bool active;
	bool keep;

	struct ostream *teststream;
};

struct sieve_multiscript *sieve_multiscript_start_execute
(struct sieve_instance *svinst,	const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv)
{
	pool_t pool;
	struct sieve_result *result;
	struct sieve_multiscript *mscript;

	result = sieve_result_create(svinst, msgdata, senv, NULL);
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
(struct sieve_multiscript *mscript, struct sieve_error_handler *ehandler,
	bool *keep)
{
	sieve_result_set_error_handler(mscript->result, ehandler);

	if ( mscript->status > 0 ) {
		mscript->status = sieve_result_print
			(mscript->result, mscript->scriptenv, mscript->teststream, keep);
	} else {
		if ( keep != NULL ) *keep = TRUE;
	}

	sieve_result_mark_executed(mscript->result);
}

static void sieve_multiscript_execute
(struct sieve_multiscript *mscript, struct sieve_error_handler *ehandler,
	bool *keep)
{
	sieve_result_set_error_handler(mscript->result, ehandler);

	if ( mscript->status > 0 ) {
		mscript->status = sieve_result_execute(mscript->result, keep);
	} else {
		if ( !sieve_result_implicit_keep(mscript->result) )
			mscript->status = SIEVE_EXEC_KEEP_FAILED;
		else
			if ( keep != NULL ) *keep = TRUE;
	}
}

bool sieve_multiscript_run
(struct sieve_multiscript *mscript, struct sieve_binary *sbin,
	struct sieve_error_handler *ehandler, enum sieve_runtime_flags flags,
	bool final)
{
	if ( !mscript->active ) return FALSE;

	if ( final )
		sieve_result_set_keep_action(mscript->result, NULL, &act_store);

	/* Run the script */
	mscript->status = sieve_run(sbin, &mscript->result, mscript->msgdata,
		mscript->scriptenv, ehandler, flags);

	if ( mscript->status >= 0 ) {
		mscript->keep = FALSE;

		if ( mscript->teststream != NULL )
			sieve_multiscript_test(mscript, ehandler, &mscript->keep);
		else
			sieve_multiscript_execute(mscript, ehandler, &mscript->keep);

		mscript->active =
			( mscript->active && mscript->keep && !final && mscript->status > 0 );
	}

	if ( mscript->status <= 0 )
		return FALSE;

	return mscript->active;
}

int sieve_multiscript_status(struct sieve_multiscript *mscript)
{
	return mscript->status;
}

int sieve_multiscript_tempfail(struct sieve_multiscript **mscript,
	struct sieve_error_handler *ehandler)
{
	struct sieve_result *result = (*mscript)->result;
	int ret = (*mscript)->status;

	if ( ehandler != NULL )
		sieve_result_set_error_handler(result, ehandler);

	if ( (*mscript)->active ) {
		ret = SIEVE_EXEC_TEMP_FAILURE;

		if ( !(*mscript)->teststream && sieve_result_executed(result) ) {
			/* Part of the result is already executed, need to fall back to
			 * to implicit keep (FIXME)
			 */
			switch ( sieve_result_implicit_keep(result) ) {
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
	*mscript = NULL;

	return ret;
}

int sieve_multiscript_finish(struct sieve_multiscript **mscript,
	struct sieve_error_handler *ehandler, bool *keep)
{
	struct sieve_result *result = (*mscript)->result;
	int ret = (*mscript)->status;

	if ( ehandler != NULL )
		sieve_result_set_error_handler(result, ehandler);

	if ( (*mscript)->active ) {
		ret = SIEVE_EXEC_FAILURE;

		if ( (*mscript)->teststream ) {
			(*mscript)->keep = TRUE;
		} else {
			switch ( sieve_result_implicit_keep(result) ) {
			case SIEVE_EXEC_OK:
				(*mscript)->keep = TRUE;
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

	if ( keep != NULL ) *keep = (*mscript)->keep;

	/* Cleanup */
	sieve_result_unref(&result);
	*mscript = NULL;

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
 * Script directory
 */

struct sieve_directory {
		struct sieve_instance *svinst;
		DIR *dirp;

		const char *path;
};

struct sieve_directory *sieve_directory_open
(struct sieve_instance *svinst, const char *path, enum sieve_error *error_r)
{
	struct sieve_directory *sdir = NULL;
	DIR *dirp;
	struct stat st;

	if ( error_r != NULL )
		*error_r = SIEVE_ERROR_NONE;

	if ( (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) ||
		(((svinst->flags & SIEVE_FLAG_HOME_RELATIVE) != 0 ) && path[0] != '/') ) {
		/* Home-relative path. change to absolute. */
		const char *home = sieve_environment_get_homedir(svinst);

		if ( home != NULL ) {
			if ( path[0] == '~' && (path[1] == '/' || path[1] == '\0') )
				path = home_expand_tilde(path, home);
			else
				path = t_strconcat(home, "/", path, NULL);
		} else {
			sieve_sys_error(svinst,
				"sieve dir path %s is relative to home directory, "
				"but home directory is not available.", path);
			if ( error_r != NULL )
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
			return NULL;
		}
	}

	/* Specified path can either be a regular file or a directory */
	if ( stat(path, &st) != 0 ) {
		switch ( errno ) {
		case ENOENT:
			if ( error_r != NULL )
				*error_r = SIEVE_ERROR_NOT_FOUND;
			break;
		case EACCES:
			sieve_sys_error(svinst, "failed to open sieve dir: %s",
				eacces_error_get("stat", path));
			if ( error_r != NULL )
				*error_r = SIEVE_ERROR_NO_PERMISSION;
			break;
		default:
			sieve_sys_error(svinst, "failed to open sieve dir: stat(%s) failed: %m",
				path);
			if ( error_r != NULL )
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
			break;
		}
		return NULL;
	}

	if ( S_ISDIR(st.st_mode) ) {

		/* Open the directory */
		if ( (dirp = opendir(path)) == NULL ) {
			switch ( errno ) {
			case ENOENT:
				if ( error_r != NULL )
					*error_r = SIEVE_ERROR_NOT_FOUND;
				break;
			case EACCES:
				sieve_sys_error(svinst, "failed to open sieve dir: %s",
					eacces_error_get("opendir", path));
				if ( error_r != NULL )
					*error_r = SIEVE_ERROR_NO_PERMISSION;
				break;
			default:
				sieve_sys_error(svinst, "failed to open sieve dir: opendir(%s) failed: "
					"%m", path);
				if ( error_r != NULL )
					*error_r = SIEVE_ERROR_TEMP_FAILURE;
				break;
			}
			return NULL;
		}

		/* Create object */
		sdir = t_new(struct sieve_directory, 1);
		sdir->path = path;
		sdir->dirp = dirp;
	} else {
		sdir = t_new(struct sieve_directory, 1);
		sdir->path = path;
		sdir->dirp = NULL;
	}

	sdir->svinst = svinst;

	return sdir;
}

const char *sieve_directory_get_scriptfile(struct sieve_directory *sdir)
{
	const char *script = NULL;
	struct dirent *dp;

	if ( sdir->dirp != NULL ) {
		while ( script == NULL ) {
			const char *file;
			struct stat st;

			errno = 0;
			if ( (dp = readdir(sdir->dirp)) == NULL ) {
				if ( errno != 0 ) {
					sieve_sys_error(sdir->svinst, "failed to read sieve dir: "
						"readdir(%s) failed: %m", sdir->path);
				}

				return NULL;
			}

			if ( !sieve_scriptfile_has_extension(dp->d_name) )
				continue;

			if ( sdir->path[strlen(sdir->path)-1] == '/' )
				file = t_strconcat(sdir->path, dp->d_name, NULL);
			else
				file = t_strconcat(sdir->path, "/", dp->d_name, NULL);

			if ( stat(file, &st) != 0 || !S_ISREG(st.st_mode) )
				continue;

			script = file;
		}
	} else {
		script = sdir->path;
		sdir->path = NULL;
	}

	return script;
}

void sieve_directory_close(struct sieve_directory **sdir)
{
	/* Close the directory */
	if ( (*sdir)->dirp != NULL && closedir((*sdir)->dirp) < 0 )
		sieve_sys_error((*sdir)->svinst, "failed to close sieve dir: "
			"closedir(%s) failed: %m", (*sdir)->path);

	*sdir = NULL;
}



