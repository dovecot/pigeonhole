/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "path-util.h"
#include "istream.h"
#include "time-util.h"
#include "eacces-error.h"

#include "sieve-binary.h"
#include "sieve-script-private.h"

#include "sieve-file-storage.h"

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>

/*
 * Filename to name/name to filename
 */

const char *sieve_script_file_get_scriptname(const char *filename)
{
	const char *ext;

	/* Extract the script name */
	ext = strrchr(filename, '.');
	if ( ext == NULL || ext == filename ||
		strcmp(ext, "."SIEVE_SCRIPT_FILEEXT) != 0 )
		return NULL;

	return t_strdup_until(filename, ext);
}

bool sieve_script_file_has_extension(const char *filename)
{
	return ( sieve_script_file_get_scriptname(filename) != NULL );
}

const char *sieve_script_file_from_name(const char *name)
{
	return t_strconcat(name, "."SIEVE_SCRIPT_FILEEXT, NULL);
}

/*
 * Common error handling
 */

static void sieve_file_script_handle_error
(struct sieve_file_script *fscript, const char *op, const char *path,
	const char *name, enum sieve_error *error_r)
{
	struct sieve_script *script = &fscript->script;
	const char *abspath, *error;

	switch ( errno ) {
	case ENOENT:
		if (t_abspath(path, &abspath, &error) < 0) {
			sieve_script_set_error(script,
				SIEVE_ERROR_TEMP_FAILURE,
				"t_abspath(%s) failed: %s",
				path, error);
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			break;
		}
		sieve_script_sys_debug(script, "File `%s' not found",
				       abspath);
		sieve_script_set_error(script,
			SIEVE_ERROR_NOT_FOUND,
			"Sieve script `%s' not found", name);
		*error_r = SIEVE_ERROR_NOT_FOUND;
		break;
	case EACCES:
		sieve_script_set_critical(script,
			"Failed to %s sieve script: %s",
			op, eacces_error_get(op, path));
		*error_r = SIEVE_ERROR_NO_PERMISSION;
		break;
	default:
		sieve_script_set_critical(script,
			"Failed to %s sieve script: %s(%s) failed: %m",
			op, op, path);
		*error_r = SIEVE_ERROR_TEMP_FAILURE;
		break;
	}
}

/*
 *
 */

static struct sieve_file_script *sieve_file_script_alloc(void)
{
	struct sieve_file_script *fscript;
	pool_t pool;

	pool = pool_alloconly_create("sieve_file_script", 2048);
	fscript = p_new(pool, struct sieve_file_script, 1);
	fscript->script = sieve_file_script;
	fscript->script.pool = pool;

	return fscript;
}

struct sieve_file_script *sieve_file_script_init_from_filename
(struct sieve_file_storage *fstorage, const char *filename,
	const char *scriptname)
{
	struct sieve_storage *storage = &fstorage->storage;
	struct sieve_file_script *fscript = NULL;

	/* Prevent initializing the active script link as a script when it
	 * resides in the sieve storage directory.
	 */
	if ( scriptname != NULL && fstorage->link_path != NULL &&
		*(fstorage->link_path) == '\0' ) {
		if ( strcmp(filename, fstorage->active_fname) == 0 ) {
			sieve_storage_set_error(storage,
				SIEVE_ERROR_NOT_FOUND,
				"Script `%s' does not exist.", scriptname);
			return NULL;
		}
	}

	fscript = sieve_file_script_alloc();
	sieve_script_init
		(&fscript->script, storage, &sieve_file_script,
			sieve_file_storage_path_extend(fstorage, filename), scriptname);
	fscript->filename = p_strdup(fscript->script.pool, filename);
	return fscript;
}

struct sieve_file_script *sieve_file_script_open_from_filename
(struct sieve_file_storage *fstorage, const char *filename,
	const char *scriptname)
{
	struct sieve_file_script *fscript;
	enum sieve_error error;

	fscript = sieve_file_script_init_from_filename
		(fstorage, filename, scriptname);
	if ( fscript == NULL )
		return NULL;

	if ( sieve_script_open(&fscript->script, &error) < 0 ) {
		struct sieve_script *script = &fscript->script;
		sieve_script_unref(&script);
		return NULL;
	}

	return fscript;
}

struct sieve_file_script *sieve_file_script_init_from_name
(struct sieve_file_storage *fstorage, const char *name)
{
	struct sieve_storage *storage = &fstorage->storage;
	struct sieve_file_script *fscript;

	if (name != NULL && S_ISDIR(fstorage->st.st_mode)) {
		return sieve_file_script_init_from_filename
			(fstorage, sieve_script_file_from_name(name), name);
	}

	fscript = sieve_file_script_alloc();
	sieve_script_init
		(&fscript->script, storage, &sieve_file_script,
			fstorage->active_path, name);
	return fscript;
}

struct sieve_file_script *sieve_file_script_open_from_name
(struct sieve_file_storage *fstorage, const char *name)
{
	struct sieve_file_script *fscript;
	enum sieve_error error;

	fscript = sieve_file_script_init_from_name(fstorage, name);
	if ( fscript == NULL )
		return NULL;

	if ( sieve_script_open(&fscript->script, &error) < 0 ) {
		struct sieve_script *script = &fscript->script;
		sieve_script_unref(&script);
		return NULL;
	}

	return fscript;
}

struct sieve_file_script *sieve_file_script_init_from_path
(struct sieve_file_storage *fstorage, const char *path,
	const char *scriptname, enum sieve_error *error_r)
{
	struct sieve_instance *svinst = fstorage->storage.svinst;
	struct sieve_file_storage *fsubstorage;
	struct sieve_file_script *fscript;
	struct sieve_storage *substorage;
	enum sieve_error error;

	if ( error_r != NULL )
		*error_r = SIEVE_ERROR_NONE;
	else
		error_r = &error;

	fsubstorage = sieve_file_storage_init_from_path
		(svinst, path, 0, error_r);
	if (fsubstorage == NULL)
		return NULL;
	substorage = &fsubstorage->storage;

	fscript = sieve_file_script_alloc();
	sieve_script_init(&fscript->script,
		substorage, &sieve_file_script, path, scriptname);
	sieve_storage_unref(&substorage);

	return fscript;
}

struct sieve_file_script *sieve_file_script_open_from_path
(struct sieve_file_storage *fstorage, const char *path,
	const char *scriptname, enum sieve_error *error_r)
{
	struct sieve_storage *storage = &fstorage->storage;
	struct sieve_file_script *fscript;
	enum sieve_error error;

	if ( error_r != NULL )
		*error_r = SIEVE_ERROR_NONE;
	else
		error_r = &error;

	fscript = sieve_file_script_init_from_path
		(fstorage, path, scriptname, error_r);
	if (fscript == NULL) {
		sieve_storage_set_error(storage,
			*error_r, "Failed to open script");
		return NULL;
	}

	if ( sieve_script_open(&fscript->script, error_r) < 0 ) {
		struct sieve_script *script = &fscript->script;
		const char *errormsg;

		errormsg = sieve_script_get_last_error(&fscript->script, error_r);
		sieve_storage_set_error(storage,
			*error_r, "%s", errormsg);
		sieve_script_unref(&script);
		return NULL;
	}

	return fscript;
}

/*
 * Open
 */

static int sieve_file_script_stat
(const char *path, struct stat *st, struct stat *lnk_st)
{
	if ( lstat(path, st) < 0 )
		return -1;

	*lnk_st = *st;

	if ( S_ISLNK(st->st_mode) && stat(path, st) < 0 )
		return -1;

	return 0;
}

static const char *
path_split_filename(const char *path, const char **dirpath_r)
{
	const char *filename;

	filename = strrchr(path, '/');
	if ( filename == NULL ) {
		*dirpath_r = "";
		filename = path;
	} else {
		*dirpath_r = t_strdup_until(path, filename);
		filename++;
	}
	return filename;
}

static int sieve_file_script_open
(struct sieve_script *script, enum sieve_error *error_r)
{
	struct sieve_file_script *fscript =
		(struct sieve_file_script *)script;
	struct sieve_storage *storage = script->storage;
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	pool_t pool = script->pool;
	const char *filename, *name, *path;
	const char *dirpath, *basename, *binpath, *binprefix;
	struct stat st, lnk_st;
	bool success = TRUE;
	int ret = 0;

	filename = fscript->filename;
	basename = NULL;
	name = script->name;
	st = fstorage->st;
	lnk_st = fstorage->lnk_st;

	if (name == NULL)
		name = storage->script_name;

	T_BEGIN {
		if ( S_ISDIR(st.st_mode) ) {
			/* Storage is a directory */
			path = fstorage->path;

			if ( (filename == NULL || *filename == '\0') &&
				name != NULL && *name != '\0' ) {
				/* Name is used to find actual filename */
				filename = sieve_script_file_from_name(name);
				basename = name;
			}
			if ( filename == NULL || *filename == '\0' ) {
				sieve_script_set_critical(script,
					"Sieve script file path '%s' is a directory.", path);
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
				success = FALSE;
			}	else {
				/* Extend storage path with filename */
				if (name == NULL) {
					if ( basename == NULL &&
						(basename=sieve_script_file_get_scriptname(filename)) == NULL )
						basename = filename;
					name = basename;
				} else if (basename == NULL) {
					basename = name;
				}
				dirpath = path;

				path = sieve_file_storage_path_extend(fstorage, filename);
				ret = sieve_file_script_stat(path, &st, &lnk_st);
			}

		} else {
			/* Storage is a single file */
			path = fstorage->active_path;

			/* Extract filename from path */
			filename = path_split_filename(path, &dirpath);

			if ( (basename=sieve_script_file_get_scriptname(filename)) == NULL )
				basename = filename;

			if ( name == NULL )
				name = basename;
		}

		if ( success ) {
			if ( ret < 0 ) {
				/* Make sure we have a script name for the error */
				if ( name == NULL ) {
					i_assert( basename != NULL );
					name = basename;
				}
				sieve_file_script_handle_error
					(fscript, "stat", path, name, error_r);
				success = FALSE;

			} else if ( !S_ISREG(st.st_mode) ) {
				sieve_script_set_critical(script,
					"Sieve script file '%s' is not a regular file.", path);
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
				success = FALSE;
			}
		}

		if ( success ) {
			const char *bpath, *bfile, *bprefix;

			if ( storage->bin_dir != NULL ) {
				bpath = storage->bin_dir;
				bfile = sieve_binfile_from_name(name);
				bprefix = name;

			} else {
				bpath = dirpath;
				bfile = sieve_binfile_from_name(basename);
				bprefix = basename;
			}

			if ( *bpath == '\0' ) {
				binpath = bfile;
				binprefix = bprefix;
			} else 	if ( bpath[strlen(bpath)-1] == '/' ) {
				binpath = t_strconcat(bpath, bfile, NULL);
				binprefix = t_strconcat(bpath, bprefix, NULL);
			} else {
				binpath = t_strconcat(bpath, "/", bfile, NULL);
				binprefix = t_strconcat(bpath, "/", bprefix, NULL);
			}

			fscript->st = st;
			fscript->lnk_st = lnk_st;
			fscript->path = p_strdup(pool, path);
			fscript->filename = p_strdup(pool, filename);
			fscript->dirpath = p_strdup(pool, dirpath);
			fscript->binpath = p_strdup(pool, binpath);
			fscript->binprefix = p_strdup(pool, binprefix);

			fscript->script.location = fscript->path;

			if ( fscript->script.name == NULL )
				fscript->script.name = p_strdup(pool, basename);
		}
	} T_END;

	return ( success ? 0 : -1 );
}

static int sieve_file_script_get_stream
(struct sieve_script *script, struct istream **stream_r,
	enum sieve_error *error_r)
{
	struct sieve_file_script *fscript = (struct sieve_file_script *)script;
	struct stat st;
	struct istream *result;
	int fd;

	if ( (fd=open(fscript->path, O_RDONLY)) < 0 ) {
		sieve_file_script_handle_error
			(fscript, "open", fscript->path, fscript->script.name, error_r);
		return -1;
	}

	if ( fstat(fd, &st) != 0 ) {
		sieve_script_set_critical(script,
			"Failed to open sieve script: fstat(fd=%s) failed: %m",
			fscript->path);
		*error_r = SIEVE_ERROR_TEMP_FAILURE;
		result = NULL;
	} else {
		/* Re-check the file type just to be sure */
		if ( !S_ISREG(st.st_mode) ) {
			sieve_script_set_critical(script,
				"Sieve script file `%s' is not a regular file", fscript->path);
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			result = NULL;
		} else {
			result = i_stream_create_fd_autoclose(&fd, SIEVE_FILE_READ_BLOCK_SIZE);
			fscript->st = fscript->lnk_st = st;
		}
	}

	if ( result == NULL ) {
		/* Something went wrong, close the fd */
		if ( fd >= 0 && close(fd) != 0 ) {
			sieve_script_sys_error(script,
				"Failed to close sieve script: "
				"close(fd=%s) failed: %m", fscript->path);
		}
		return -1;
	}

	*stream_r = result;
	return 0;
}

/*
 * Binary
 */

static int sieve_file_script_binary_read_metadata
(struct sieve_script *script, struct sieve_binary_block *sblock,
	sieve_size_t *offset ATTR_UNUSED)
{
	struct sieve_file_script *fscript = (struct sieve_file_script *)script;
	struct sieve_instance *svinst = script->storage->svinst;
	struct sieve_binary *sbin = sieve_binary_block_get_binary(sblock);
	const struct stat *sstat, *bstat;

	bstat = sieve_binary_stat(sbin);
	if ( fscript->st.st_mtime > fscript->lnk_st.st_mtime ||
		(fscript->st.st_mtime == fscript->lnk_st.st_mtime &&
		 ST_MTIME_NSEC(fscript->st) >= ST_MTIME_NSEC(fscript->lnk_st)) ) {
		sstat = &fscript->st;
	} else {
		sstat = &fscript->lnk_st;
	}

	if ( bstat->st_mtime < sstat->st_mtime ||
		(bstat->st_mtime == sstat->st_mtime &&
			ST_MTIME_NSEC(*bstat) <= ST_MTIME_NSEC(*sstat)) ) {
		if ( svinst->debug ) {
			sieve_script_sys_debug(script,
				"Sieve binary `%s' is not newer "
				"than the Sieve script `%s' (%s.%lu <= %s.%lu)",
				sieve_binary_path(sbin), sieve_script_location(script),
				t_strflocaltime("%Y-%m-%d %H:%M:%S", bstat->st_mtime),
				ST_MTIME_NSEC(*bstat),
				t_strflocaltime("%Y-%m-%d %H:%M:%S", sstat->st_mtime),
				ST_MTIME_NSEC(*sstat));
		}
		return 0;
	}

	return 1;
}

static struct sieve_binary *sieve_file_script_binary_load
(struct sieve_script *script, enum sieve_error *error_r)
{
	struct sieve_file_script *fscript = (struct sieve_file_script *)script;
	struct sieve_instance *svinst = script->storage->svinst;

	return sieve_binary_open(svinst, fscript->binpath, script, error_r);
}

static int sieve_file_script_binary_save
(struct sieve_script *script, struct sieve_binary *sbin, bool update,
	enum sieve_error *error_r)
{
	struct sieve_storage *storage = script->storage;
	struct sieve_file_script *fscript = (struct sieve_file_script *)script;

	if ( storage->bin_dir != NULL &&
		sieve_storage_setup_bindir(storage, 0700) < 0 )
		return -1;

	return sieve_binary_save(sbin, fscript->binpath, update,
		fscript->st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO), error_r);
}

static const char *sieve_file_script_binary_get_prefix
(struct sieve_script *script)
{
	struct sieve_file_script *fscript = (struct sieve_file_script *)script;
	
	return fscript->binprefix;
}

/*
 * Management
 */

static int sieve_file_storage_script_is_active(struct sieve_script *script)
{
	struct sieve_file_script *fscript =
		(struct sieve_file_script *) script;
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)script->storage;
	const char *afile;
	int ret = 0;

	T_BEGIN {
		ret = sieve_file_storage_active_script_get_file(fstorage, &afile);

		if ( ret > 0 ) {
		 	/* Is the requested script active? */
			ret = ( strcmp(fscript->filename, afile) == 0 ? 1 : 0 );
		}
	} T_END;

	return ret;
}

static int sieve_file_storage_script_delete(struct sieve_script *script)
{
	struct sieve_file_script *fscript =
		(struct sieve_file_script *)script;
	int ret = 0;

	if ( sieve_file_storage_pre_modify(script->storage) < 0 )
		return -1;

	ret = unlink(fscript->path);
	if ( ret < 0 ) {
		if ( errno == ENOENT ) {
			sieve_script_set_error(script,
				SIEVE_ERROR_NOT_FOUND,
				"Sieve script does not exist.");
		} else {
			sieve_script_set_critical(script,
				"Performing unlink() failed on sieve file `%s': %m",
				fscript->path);
		}
	}
	return ret;
}

static int _sieve_file_storage_script_activate
(struct sieve_file_script *fscript)
{
	struct sieve_script *script = &fscript->script;
	struct sieve_storage *storage = script->storage;
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	struct stat st;
	const char *link_path, *afile;
	int activated = 0;
	int ret;

	/* Find out whether there is an active script, but recreate
	 * the symlink either way. This way, any possible error in the symlink
	 * resolves automatically. This step is only necessary to provide a
	 * proper return value indicating whether the script was already active.
	 */
	ret = sieve_file_storage_active_script_get_file(fstorage, &afile);

	/* Is the requested script already active? */
	if ( ret <= 0 || strcmp(fscript->filename, afile) != 0 )
		activated = 1;

	i_assert( fstorage->link_path != NULL );

	/* Check the scriptfile we are trying to activate */
	if ( lstat(fscript->path, &st) != 0 ) {
		sieve_script_set_critical(script,
		  "Failed to activate Sieve script: lstat(%s) failed: %m.",
			fscript->path);
		return -1;
	}

	/* Rescue a possible .dovecot.sieve regular file remaining from old
	 * installations.
	 */
	if ( !sieve_file_storage_active_rescue_regular(fstorage) ) {
		/* Rescue failed, manual intervention is necessary */
		return -1;
	}

	/* Just try to create the symlink first */
	link_path = t_strconcat
	  ( fstorage->link_path, fscript->filename, NULL );

 	ret = symlink(link_path, fstorage->active_path);
	if ( ret < 0 ) {
		if ( errno == EEXIST ) {
			ret = sieve_file_storage_active_replace_link(fstorage, link_path);
			if ( ret < 0 ) {
				return ret;
			}
		} else {
			/* Other error, critical */
			sieve_script_set_critical(script,
				"Failed to activate Sieve script: "
				"symlink(%s, %s) failed: %m",
					link_path, fstorage->active_path);
			return -1;
		}
	}
	return activated;
}

static int sieve_file_storage_script_activate
(struct sieve_script *script)
{
	struct sieve_file_script *fscript =
		(struct sieve_file_script *)script;
	int ret;

	if ( sieve_file_storage_pre_modify(script->storage) < 0 )
		return -1;

	T_BEGIN {
		ret = _sieve_file_storage_script_activate(fscript);
	} T_END;

	return ret;
}

static int sieve_file_storage_script_rename
(struct sieve_script *script, const char *newname)
{
	struct sieve_file_script *fscript =
		(struct sieve_file_script *)script;
	struct sieve_storage *storage = script->storage;
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	const char *newpath, *newfile, *link_path;
	int ret = 0;

	if ( sieve_file_storage_pre_modify(storage) < 0 )
		return -1;

	T_BEGIN {
		newfile = sieve_script_file_from_name(newname);
		newpath = t_strconcat( fstorage->path, "/", newfile, NULL );

		/* The normal rename() system call overwrites the existing file without
		 * notice. Also, active scripts must not be disrupted by renaming a script.
		 * That is why we use a link(newpath) [activate newpath] unlink(oldpath)
		 */

		/* Link to the new path */
		ret = link(fscript->path, newpath);
		if ( ret >= 0 ) {
			/* Is the requested script active? */
			if ( sieve_script_is_active(script) > 0 ) {
				/* Active; make active link point to the new copy */
				i_assert( fstorage->link_path != NULL );
				link_path = t_strconcat
					( fstorage->link_path, newfile, NULL );

				ret = sieve_file_storage_active_replace_link(fstorage, link_path);
			}

			if ( ret >= 0 ) {
				/* If all is good, remove the old link */
				if ( unlink(fscript->path) < 0 ) {
					sieve_script_sys_error(script,
						"Failed to clean up after rename: "
						"unlink(%s) failed: %m", fscript->path);
				}

				if ( script->name != NULL && *script->name != '\0' )
					script->name = p_strdup(script->pool, newname);
				fscript->path = p_strdup(script->pool, newpath);
				fscript->filename = p_strdup(script->pool, newfile);
			} else {
				/* If something went wrong, remove the new link to restore previous
				 * state
				 */
				if ( unlink(newpath) < 0 ) {
					sieve_script_sys_error(script,
						"Failed to clean up after failed rename: "
						"unlink(%s) failed: %m", newpath);
				}
			}
		} else {
			/* Our efforts failed right away */
			switch ( errno ) {
			case ENOENT:
				sieve_script_set_error(script, SIEVE_ERROR_NOT_FOUND,
					"Sieve script does not exist.");
				break;
			case EEXIST:
				sieve_script_set_error(script, SIEVE_ERROR_EXISTS,
					"A sieve script with that name already exists.");
				break;
			default:
				sieve_script_set_critical(script,
					"Failed to rename Sieve script: "
					"link(%s, %s) failed: %m", fscript->path, newpath);
			}
		}
	} T_END;

	return ret;
}

/*
 * Properties
 */

static int sieve_file_script_get_size
(const struct sieve_script *script, uoff_t *size_r)
{
	struct sieve_file_script *fscript = (struct sieve_file_script *)script;

	*size_r = fscript->st.st_size;
	return 1;
}

const char *sieve_file_script_get_dirpath
(const struct sieve_script *script)
{
	struct sieve_file_script *fscript = (struct sieve_file_script *)script;

	if ( script->driver_name != sieve_file_script.driver_name )
		return NULL;

	return fscript->dirpath;
}

const char *sieve_file_script_get_path
(const struct sieve_script *script)
{
	struct sieve_file_script *fscript = (struct sieve_file_script *)script;

	if ( script->driver_name != sieve_file_script.driver_name )
		return NULL;

	return fscript->path;
}

/*
 * Matching
 */

static bool sieve_file_script_equals
(const struct sieve_script *script, const struct sieve_script *other)
{
	struct sieve_file_script *fscript =
		(struct sieve_file_script *)script;
	struct sieve_file_script *fother =
		(struct sieve_file_script *)other;

	return ( CMP_DEV_T(fscript->st.st_dev, fother->st.st_dev) &&
		fscript->st.st_ino == fother->st.st_ino );
}

/*
 * Driver definition
 */

const struct sieve_script sieve_file_script = {
	.driver_name = SIEVE_FILE_STORAGE_DRIVER_NAME,
	.v = {
		.open = sieve_file_script_open,

		.get_stream = sieve_file_script_get_stream,

		.binary_read_metadata = sieve_file_script_binary_read_metadata,
		.binary_load = sieve_file_script_binary_load,
		.binary_save = sieve_file_script_binary_save,
		.binary_get_prefix = sieve_file_script_binary_get_prefix,

		.rename = sieve_file_storage_script_rename,
		.delete = sieve_file_storage_script_delete,
		.is_active = sieve_file_storage_script_is_active,
		.activate = sieve_file_storage_script_activate,

		.get_size = sieve_file_script_get_size,

		.equals = sieve_file_script_equals
	}
};

