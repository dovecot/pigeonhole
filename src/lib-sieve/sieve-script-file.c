/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "strfuncs.h"
#include "abspath.h"
#include "eacces-error.h"
#include "istream.h"
#include "home-expand.h"

#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-error.h"
#include "sieve-binary.h"
#include "sieve-script-private.h"
#include "sieve-script-file.h"

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

/*
 * Configuration
 */

#define SIEVE_FILE_READ_BLOCK_SIZE (1024*8)

/*
 * Filename to name/name to filename
 */

const char *sieve_scriptfile_get_script_name(const char *filename)
{
	const char *ext;

	/* Extract the script name */
	ext = strrchr(filename, '.');
	if ( ext == NULL || ext == filename ||
		strcmp(ext, "."SIEVE_SCRIPT_FILEEXT) != 0 )
		return NULL;

	return t_strdup_until(filename, ext);
}

bool sieve_scriptfile_has_extension(const char *filename)
{
	return ( sieve_scriptfile_get_script_name(filename) != NULL );
}

const char *sieve_scriptfile_from_name(const char *name)
{
	return t_strconcat(name, "."SIEVE_SCRIPT_FILEEXT, NULL);
}

/*
 * Common error handling
 */

static void sieve_file_script_handle_error
(struct sieve_script *script, const char *path, const char *name,
	enum sieve_error *error_r)
{
	struct sieve_instance *svinst = script->svinst;
	struct sieve_error_handler *ehandler = script->ehandler;

	switch ( errno ) {
	case ENOENT:
		if ( svinst->debug )
			sieve_sys_debug(svinst, "script file %s not found", t_abspath(path));
		*error_r = SIEVE_ERROR_NOT_FOUND;
		break;
	case EACCES:
		sieve_critical(svinst, ehandler, name, "failed to open sieve script",
			"failed to stat sieve script: %s", eacces_error_get("stat", path));
		*error_r = SIEVE_ERROR_NO_PERMISSION;
		break;
	default:
		sieve_critical(svinst, ehandler, name, "failed to open sieve script",
			"failed to stat sieve script: stat(%s) failed: %m", path);
		*error_r = SIEVE_ERROR_TEMP_FAILURE;
		break;
	}
}

/*
 * Script file implementation
 */

static int sieve_file_script_stat
(const char *path, struct stat *st, struct stat *lnk_st)
{
	if ( lstat(path, st) < 0 )
		return -1;

	*lnk_st = *st;

	if ( S_ISLNK(st->st_mode) && stat(path, st) < 0 )
		return -1;

	return 1;
}

static struct sieve_script *sieve_file_script_alloc(void)
{
	struct sieve_file_script *script;
	pool_t pool;

	pool = pool_alloconly_create("sieve_file_script", 1024);
	script = p_new(pool, struct sieve_file_script, 1);
	script->script = sieve_file_script;
	script->script.pool = pool;

	return &script->script;
}

static int sieve_file_script_open
(struct sieve_script *_script, const char *path, const char *const *options,
	enum sieve_error *error_r)
{
	struct sieve_file_script *script = (struct sieve_file_script *)_script;
	struct sieve_instance *svinst = _script->svinst;
	struct sieve_error_handler *ehandler = _script->ehandler;
	pool_t pool = _script->pool;
	const char *name = _script->name;
	const char *filename, *dirpath, *basename, *binpath;
	struct stat st;
	struct stat lnk_st;
	bool success = TRUE;
	int ret;

	if ( options != NULL && *options != NULL ) {
		const char *option = *options;

		sieve_critical(svinst, ehandler, NULL, "failed to open sieve script",
			"sieve file backend: invalid option `%s'", option);
		*error_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}

	T_BEGIN {
		if ( (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) ||
			(((svinst->flags & SIEVE_FLAG_HOME_RELATIVE) != 0 ) && path[0] != '/') ) {
			/* home-relative path. change to absolute. */
			const char *home = sieve_environment_get_homedir(svinst);

			if ( home != NULL ) {
				if ( path[0] == '~' && (path[1] == '/' || path[1] == '\0') )
					path = home_expand_tilde(path, home);
				else
					path = t_strconcat(home, "/", path, NULL);
			} else {
				sieve_critical(svinst, ehandler, NULL,
					"failed to open sieve script",
					"sieve script file path %s is relative to home directory, "
					"but home directory is not available.", path);
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
				success = FALSE;
			}
		}

		if ( success && (ret=sieve_file_script_stat(path, &st, &lnk_st)) > 0 ) {
			if ( S_ISDIR(st.st_mode) ) {
				/* Path is directory; name is used to find actual file */
				if (name == 0 || *name == '\0') {
					sieve_critical(svinst, ehandler, NULL,
						"failed to open sieve script",
						"sieve script file path '%s' is a directory.", path);
					*error_r = SIEVE_ERROR_TEMP_FAILURE;
					success = FALSE;
				}	else {
					/* Extend path with filename */
					filename = sieve_scriptfile_from_name(name);
					basename = name;
					dirpath = path;

					if ( path[strlen(path)-1] == '/' )
						path = t_strconcat(dirpath, filename, NULL);
					else
						path = t_strconcat(dirpath, "/", filename , NULL);

					ret = sieve_file_script_stat(path, &st, &lnk_st);
				}

			} else {

				/* Extract filename from path */
				filename = strrchr(path, '/');
				if ( filename == NULL ) {
					dirpath = "";
					filename = path;
				} else {
					dirpath = t_strdup_until(path, filename);
					filename++;
				}

				if ( (basename=sieve_scriptfile_get_script_name(filename)) == NULL )
					basename = filename;

				if ( name == NULL )
					name = basename;
			}
		} else {
			basename = name;
		}

		if ( success ) {
			if ( ret <= 0 ) {
				sieve_file_script_handle_error(_script, path, name, error_r);
				success = FALSE;
			} else if (!S_ISREG(st.st_mode) ) {
				sieve_critical(svinst, ehandler, name,
					"failed to open sieve script",
					"sieve script file '%s' is not a regular file.", path);
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
				success = FALSE;
			}
		}

		if ( success ) {
			if ( _script->bin_dir != NULL ) {
				binpath = sieve_binfile_from_name(name);
				binpath = t_strconcat(_script->bin_dir, "/", binpath, NULL);
			} else {
				binpath = sieve_binfile_from_name(basename);
				if ( *dirpath != '\0' )
					binpath = t_strconcat(dirpath, "/", binpath, NULL);
			}

			script->st = st;
			script->lnk_st = lnk_st;
			script->path = p_strdup(pool, path);
			script->filename = p_strdup(pool, filename);
			script->dirpath = p_strdup(pool, dirpath);
			script->binpath = p_strdup(pool, binpath);

			if ( script->script.name == NULL ||
				strcmp(script->script.name, basename) == 0 )
				script->script.location = script->path;
			else
				script->script.location = p_strconcat
					(pool, script->path, ";name=", script->script.name, NULL);

			if ( script->script.name == NULL )
				script->script.name = p_strdup(pool, basename);
		}
	} T_END;

	return ( success ? 0 : -1 );
}

static int sieve_file_script_get_stream
(struct sieve_script *_script, struct istream **stream_r,
	enum sieve_error *error_r)
{
	struct sieve_file_script *script = (struct sieve_file_script *)_script;
	struct sieve_instance *svinst = _script->svinst;
	struct sieve_error_handler *ehandler = _script->ehandler;
	const char *name = _script->name;
	struct stat st;
	struct istream *result;
	int fd;

	if ( (fd=open(script->path, O_RDONLY)) < 0 ) {
		sieve_file_script_handle_error(_script, script->path, name, error_r);
		return -1;
	}

	if ( fstat(fd, &st) != 0 ) {
		sieve_critical(svinst, ehandler, name,
			"failed to open sieve script",
			"failed to open sieve script: fstat(fd=%s) failed: %m", script->path);
		*error_r = SIEVE_ERROR_TEMP_FAILURE;
		result = NULL;
	} else {
		/* Re-check the file type just to be sure */
		if ( !S_ISREG(st.st_mode) ) {
			sieve_critical(svinst, ehandler, name,
				"failed to open sieve script",
				"sieve script file '%s' is not a regular file", script->path);
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			result = NULL;
		} else {
			result = i_stream_create_fd(fd, SIEVE_FILE_READ_BLOCK_SIZE, TRUE);
			script->st = script->lnk_st = st;
		}
	}

	if ( result == NULL ) {
		/* Something went wrong, close the fd */
		if ( close(fd) != 0 ) {
			sieve_sys_error(svinst,
				"failed to close sieve script: close(fd=%s) failed: %m", script->path);
		}
	}

	*stream_r = result;
	return 0;
}

static int sieve_file_script_get_size
(const struct sieve_script *_script, uoff_t *size_r)
{
	struct sieve_file_script *script = (struct sieve_file_script *)_script;

	*size_r = script->st.st_size;
	return 1;
}

static bool sieve_file_script_equals
(const struct sieve_script *_script, const struct sieve_script *_other)
{
	struct sieve_file_script *script = (struct sieve_file_script *)_script;
	struct sieve_file_script *other = (struct sieve_file_script *)_other;

	if ( script == NULL || other == NULL )
		return FALSE;

	return ( script->st.st_ino == other->st.st_ino );
}

static int sieve_file_script_binary_read_metadata
(struct sieve_script *_script, struct sieve_binary_block *sblock,
	sieve_size_t *offset ATTR_UNUSED)
{
	struct sieve_file_script *script = (struct sieve_file_script *)_script;
	struct sieve_binary *sbin = sieve_binary_block_get_binary(sblock);
	time_t time = ( script->st.st_mtime > script->lnk_st.st_mtime ?
		script->st.st_mtime : script->lnk_st.st_mtime );

	if ( sieve_binary_mtime(sbin) <= time )
		return 0;

	return 1;
}

static struct sieve_binary *sieve_file_script_binary_load
(struct sieve_script *_script, enum sieve_error *error_r)
{
	struct sieve_file_script *script = (struct sieve_file_script *)_script;

	return sieve_binary_open(_script->svinst, script->binpath, _script, error_r);
}

static int sieve_file_script_binary_save
(struct sieve_script *_script, struct sieve_binary *sbin, bool update,
	enum sieve_error *error_r)
{
	struct sieve_file_script *script = (struct sieve_file_script *)_script;

	if ( _script->bin_dir != NULL && sieve_script_setup_bindir(_script, 0700) < 0 )
		return -1;

	return sieve_binary_save(sbin, script->binpath, update,
		script->st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO), error_r);
}

const struct sieve_script sieve_file_script = {
	.driver_name = SIEVE_FILE_SCRIPT_DRIVER_NAME,
	.v = {
		sieve_file_script_alloc,
		NULL,

		sieve_file_script_open,

		sieve_file_script_get_stream,

		sieve_file_script_binary_read_metadata,
		NULL,
		sieve_file_script_binary_load,
		sieve_file_script_binary_save,

		sieve_file_script_get_size,

		sieve_file_script_equals
	}
};

const char *sieve_file_script_get_dirpath
(const struct sieve_script *_script)
{
	struct sieve_file_script *script = (struct sieve_file_script *)_script;

	if ( _script->driver_name != sieve_file_script.driver_name )
		return NULL;

	return script->dirpath;
}

const char *sieve_file_script_get_path
(const struct sieve_script *_script)
{
	struct sieve_file_script *script = (struct sieve_file_script *)_script;

	if ( _script->driver_name != sieve_file_script.driver_name )
		return NULL;

	return script->path;
}

