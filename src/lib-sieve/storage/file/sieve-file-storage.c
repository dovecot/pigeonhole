/* Copyright (c) 2002-2014 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "home-expand.h"
#include "ioloop.h"
#include "mkdir-parents.h"
#include "eacces-error.h"
#include "unlink-old-files.h"
#include "mail-storage-private.h"

#include "sieve.h"
#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-error-private.h"
#include "sieve-settings.h"

#include "sieve-file-storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <utime.h>
#include <sys/time.h>


#define MAX_DIR_CREATE_MODE 0770

/*
 * Utility
 */

const char *sieve_file_storage_path_extend
(struct sieve_file_storage *fstorage, const char *filename)
{
	const char *path = fstorage->path;

	if ( path[strlen(path)-1] == '/' )
		return t_strconcat(path, filename, NULL);

	return t_strconcat(path, "/", filename , NULL);
}

/*
 *
 */

static const char *sieve_storage_get_relative_link_path
	(const char *active_path, const char *storage_dir)
{
	const char *link_path, *p;
	size_t pathlen;

	/* Determine to what extent the sieve storage and active script
	 * paths match up. This enables the managed symlink to be short and the
	 * sieve storages can be moved around without trouble (if the active
	 * script path is common to the script storage).
	 */
	p = strrchr(active_path, '/');
	if ( p == NULL ) {
		link_path = storage_dir;
	} else {
		pathlen = p - active_path;

		if ( strncmp( active_path, storage_dir, pathlen ) == 0 &&
			(storage_dir[pathlen] == '/' || storage_dir[pathlen] == '\0') )
		{
			if ( storage_dir[pathlen] == '\0' )
				link_path = "";
			else
				link_path = storage_dir + pathlen + 1;
		} else
			link_path = storage_dir;
	}

	/* Add trailing '/' when link path is not empty
	 */
	pathlen = strlen(link_path);
    if ( pathlen != 0 && link_path[pathlen-1] != '/')
        return t_strconcat(link_path, "/", NULL);

	return t_strdup(link_path);
}

static mode_t get_dir_mode(mode_t mode)
{
	/* Add the execute bit if either read or write bit is set */

	if ((mode & 0600) != 0) mode |= 0100;
	if ((mode & 0060) != 0) mode |= 0010;
	if ((mode & 0006) != 0) mode |= 0001;

	return mode;
}

static void sieve_file_storage_get_permissions
(struct sieve_storage *storage, const char *path,
mode_t *file_mode_r, mode_t *dir_mode_r, gid_t *gid_r,
	const char **gid_origin_r)
{
	struct stat st;

	/* Use safe defaults */
	*file_mode_r = 0600;
	*dir_mode_r = 0700;
	*gid_r = (gid_t)-1;
	*gid_origin_r = "defaults";

	if ( stat(path, &st) < 0 ) {
		if ( !ENOTFOUND(errno) ) {
			sieve_storage_sys_error(storage,
				"stat(%s) failed: %m", path);
		} else {
			sieve_storage_sys_debug(storage,
				"Permission lookup failed from %s", path);
		}
		return;

	} else {
		*file_mode_r = (st.st_mode & 0666) | 0600;
		*dir_mode_r = (st.st_mode & 0777) | 0700;
		*gid_origin_r = path;

		if ( !S_ISDIR(st.st_mode) ) {
			/* We're getting permissions from a file. Apply +x modes as necessary. */
			*dir_mode_r = get_dir_mode(*dir_mode_r);
		}

		if (S_ISDIR(st.st_mode) && (st.st_mode & S_ISGID) != 0) {
			/* Directory's GID is used automatically for new files */
			*gid_r = (gid_t)-1;
		} else if ((st.st_mode & 0070) >> 3 == (st.st_mode & 0007)) {
			/* Group has same permissions as world, so don't bother changing it */
			*gid_r = (gid_t)-1;
		} else if (getegid() == st.st_gid) {
			/* Using our own gid, no need to change it */
			*gid_r = (gid_t)-1;
		} else {
			*gid_r = st.st_gid;
		}
	}

	sieve_storage_sys_debug(storage,
		"Using permissions from %s: mode=0%o gid=%ld",
		path, (int)*dir_mode_r, *gid_r == (gid_t)-1 ? -1L : (long)*gid_r);
}

static int mkdir_verify
(struct sieve_storage *storage, const char *dir,
	mode_t mode, gid_t gid, const char *gid_origin)
{
	struct stat st;

	if ( stat(dir, &st) == 0 )
		return 0;

	if ( errno == EACCES ) {
		sieve_storage_sys_error(storage,
			"mkdir_verify: %s", eacces_error_get("stat", dir));
		return -1;
	} else if ( errno != ENOENT ) {
		sieve_storage_sys_error(storage,
			"mkdir_verify: stat(%s) failed: %m", dir);
		return -1;
	}

	if ( mkdir_parents_chgrp(dir, mode, gid, gid_origin) == 0 ) {
		sieve_storage_sys_debug(storage,
			"Created storage directory %s", dir);
		return 0;
	}

	switch ( errno ) {
	case EEXIST:
		return 0;
	case ENOENT:
		sieve_storage_sys_error(storage,
			"Storage was deleted while it was being created");
		break;
	case EACCES:
		sieve_storage_sys_error(storage,
			"%s",	eacces_error_get_creating("mkdir_parents_chgrp", dir));
		break;
	default:
		sieve_storage_sys_error(storage,
			"mkdir_parents_chgrp(%s) failed: %m", dir);
		break;
	}

	return -1;
}

static int check_tmp(struct sieve_storage *storage, const char *path)
{
	struct stat st;

	/* If tmp/ directory exists, we need to clean it up once in a while */
	if ( stat(path, &st) < 0 ) {
		if ( errno == ENOENT )
			return 0;
		if ( errno == EACCES ) {
			sieve_storage_sys_error(storage,
				"check_tmp: %s", eacces_error_get("stat", path));
			return -1;
		}
		sieve_storage_sys_error(storage,
			"check_tmp: stat(%s) failed: %m", path);
		return -1;
	}

	if ( st.st_atime > st.st_ctime + SIEVE_FILE_STORAGE_TMP_DELETE_SECS ) {
		/* The directory should be empty. we won't do anything
		   until ctime changes. */
	} else if ( st.st_atime < ioloop_time - SIEVE_FILE_STORAGE_TMP_SCAN_SECS ) {
		/* Time to scan */
		(void)unlink_old_files(path, "",
			ioloop_time - SIEVE_FILE_STORAGE_TMP_DELETE_SECS);
	}
	return 1;
}

static struct sieve_storage *sieve_file_storage_alloc(void)
{
	struct sieve_file_storage *fstorage;
	pool_t pool;

	pool = pool_alloconly_create("sieve_file_storage", 1024);
	fstorage = p_new(pool, struct sieve_file_storage, 1);
	fstorage->storage = sieve_file_storage;
	fstorage->storage.pool = pool;

	return &fstorage->storage;
}

static int sieve_file_storage_init_paths
(struct sieve_file_storage *fstorage, const char *active_path,
	const char *storage_path, enum sieve_error *error_r)
	ATTR_NULL(2, 3)
{
	struct sieve_storage *storage = &fstorage->storage;
	struct sieve_instance *svinst = storage->svinst;
	const char *tmp_dir, *link_path, *path, *active_fname;
	mode_t dir_create_mode, file_create_mode;
	gid_t file_create_gid;
	const char *file_create_gid_origin;
	int ret;

	fstorage->prev_mtime = (time_t)-1;

	/* Active script path */

	if ( storage->main_storage ||
		(storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0 ) {
		if ( active_path == NULL || *active_path == '\0' ) {
			sieve_storage_sys_debug(storage,
				"Active script path is unconfigured; "
				"using default (path=%s)", SIEVE_FILE_DEFAULT_PATH);
			active_path = SIEVE_FILE_DEFAULT_PATH;
		}
	}

	path = active_path;
	if ( path != NULL && *path != '\0' &&
		((path[0] == '~' && (path[1] == '/' || path[1] == '\0')) ||
		(((svinst->flags & SIEVE_FLAG_HOME_RELATIVE) != 0 ) && path[0] != '/'))
		)	{
		/* home-relative path. change to absolute. */
		const char *home = sieve_environment_get_homedir(svinst);

		if ( home != NULL ) {
			if ( path[0] == '~' && (path[1] == '/' || path[1] == '\0') )
				path = home_expand_tilde(path, home);
			else
				path = t_strconcat(home, "/", path, NULL);
		} else {
			sieve_storage_set_critical(storage,
				"Sieve storage path `%s' is relative to home directory, "
				"but home directory is not available.", path);
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			return -1;
		}
	}
	active_path = path;

	/* Get the filename for the active script link */

	active_fname = NULL;
	if ( active_path != NULL && *active_path != '\0' ) {
		active_fname = strrchr(active_path, '/');
		if ( active_fname == NULL )
			active_fname = active_path;
		else
			active_fname++;

		if ( *active_fname == '\0' ) {
			/* Link cannot be just a path ending in '/' */
			sieve_storage_set_critical(storage,
				"Path to active symlink must include the link's filename "
				"(path=%s)", active_path);
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			return -1;
		}
	}

	/* Storage path */

	path = storage_path;
	if ( path != NULL &&
		((path[0] == '~' && (path[1] == '/' || path[1] == '\0')) ||
		(((svinst->flags & SIEVE_FLAG_HOME_RELATIVE) != 0 ) && path[0] != '/')) ) {
		/* home-relative path. change to absolute. */
		const char *home = sieve_environment_get_homedir(svinst);

		if ( home != NULL ) {
			if ( path[0] == '~' && (path[1] == '/' || path[1] == '\0') )
				path = home_expand_tilde(path, home);
			else
				path = t_strconcat(home, "/", path, NULL);
		} else {
			sieve_storage_set_critical(storage,
				"Sieve storage path `%s' is relative to home directory, "
				"but home directory is not available.", path);
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			return -1;
		}
	}
	storage_path = path;

	if (storage_path == NULL || *storage_path == '\0') {
		sieve_storage_set_critical(storage,
			"Storage path cannot be empty");
		*error_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}
	
	sieve_storage_sys_debug(storage,
		"Using script storage path: %s", storage_path);

	fstorage->path = p_strdup(storage->pool, storage_path);

	if ( active_path != NULL && *active_path != '\0' ) {
		sieve_storage_sys_debug(storage,
			"Using active Sieve script path: %s", active_path);

		fstorage->active_path = p_strdup(storage->pool, active_path);
		fstorage->active_fname = p_strdup(storage->pool, active_fname);

		/* Get the path to be prefixed to the script name in the symlink pointing
		 * to the active script.
		 */
		link_path = sieve_storage_get_relative_link_path
			(fstorage->active_path, fstorage->path);

		sieve_storage_sys_debug(storage,
			"Relative path to sieve storage in active link: %s",
			link_path);

		fstorage->link_path = p_strdup(storage->pool, link_path);
	}

	/* Prepare for write access */

	if ( (storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0 ) {
		/* Get permissions */
		sieve_file_storage_get_permissions(storage,
			fstorage->path, &file_create_mode, &dir_create_mode, &file_create_gid,
			&file_create_gid_origin);

		/*
		 * Ensure sieve local directory structure exists (full autocreate):
		 *  This currently only consists of a ./tmp direcory
		 */

		tmp_dir = t_strconcat(fstorage->path, "/tmp", NULL);

		/* Try to find and clean up tmp dir */
		if ( (ret=check_tmp(storage, tmp_dir)) < 0 ) {
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			return -1;
		}

		/* Auto-create if necessary */
		if ( ret == 0 && mkdir_verify(storage, tmp_dir,
			dir_create_mode, file_create_gid, file_create_gid_origin) < 0 ) {
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			return -1;
		}

		fstorage->dir_create_mode = dir_create_mode;
		fstorage->file_create_mode = file_create_mode;
		fstorage->file_create_gid = file_create_gid;
	}

	return 0;
}

static int sieve_file_storage_init
(struct sieve_storage *storage, const char *const *options,
	enum sieve_error *error_r)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	const char *active_path = "";

	if ( options != NULL ) {
		while ( *options != NULL ) {
			const char *option = *options;

			if ( strncasecmp(option, "active=", 7) == 0 && option[7] != '\0' ) {
				active_path = option+7;
			} else {
				sieve_storage_set_critical(storage,
					"Invalid option `%s'", option);
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
				return -1;
			}

			options++;
		}
	}

	return sieve_file_storage_init_paths
		(fstorage, active_path, storage->location, error_r);
}

struct sieve_storage *sieve_file_storage_init_legacy
(struct sieve_instance *svinst, const char *active_path,
	const char *storage_path, enum sieve_storage_flags flags,
	enum sieve_error *error_r)
{
	struct sieve_storage *storage;
	struct sieve_file_storage *fstorage;

	storage = sieve_storage_alloc
		(svinst, &sieve_file_storage, "", flags, TRUE);
	storage->location = p_strdup(storage->pool, storage_path);
	fstorage = (struct sieve_file_storage *)storage;
	
	T_BEGIN {
		if ( storage_path == NULL || *storage_path == '\0' ) {
			const char *home = sieve_environment_get_homedir(svinst);

			sieve_storage_sys_debug(storage,
				"Performing auto-detection");

			/* We'll need to figure out the storage location ourself.
			 *
			 * It's $HOME/sieve or /sieve when (presumed to be) chrooted.
			 */
			if ( home != NULL && *home != '\0' ) {
				if (access(home, R_OK|W_OK|X_OK) == 0) {
					/* Use default ~/sieve */
					sieve_storage_sys_debug(storage,
						"Root exists (%s)", home);

					storage_path = t_strconcat(home, "/sieve", NULL);
				} else {
					/* Don't have required access on the home directory */

					sieve_storage_sys_debug(storage,
						"access(%s, rwx) failed: %m", home);
				}
			} else {
					sieve_storage_sys_debug(storage,
						"HOME is not set");

				if (access("/sieve", R_OK|W_OK|X_OK) == 0) {
					storage_path = "/sieve";
					sieve_storage_sys_debug(storage,
						"Directory `/sieve' exists, assuming chroot");
				}
			}
		}

		if (storage_path == NULL || *storage_path == '\0') {
			sieve_storage_set_critical(storage,
				"Could not find storage root directory; "
				"path was left unconfigured and autodetection failed");
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			sieve_storage_unref(&storage);
			storage = NULL;
		} else if (sieve_file_storage_init_paths
			(fstorage, active_path, storage_path, error_r) < 0) {
			sieve_storage_unref(&storage);
			storage = NULL;
		}
	} T_END;

	return storage;
}

struct sieve_file_storage *sieve_file_storage_init_from_path
(struct sieve_instance *svinst, const char *path,
	enum sieve_storage_flags flags, enum sieve_error *error_r)
{
	struct sieve_storage *storage;
	struct sieve_file_storage *fstorage;

	storage = sieve_storage_alloc
		(svinst, &sieve_file_storage, "", flags, FALSE);
	storage->location = p_strdup(storage->pool, path);
	fstorage = (struct sieve_file_storage *)storage;
	
	T_BEGIN {
		if ( sieve_file_storage_init_paths
			(fstorage, NULL, path, error_r) < 0 ) {
			sieve_storage_unref(&storage);
			storage = NULL;
		}
	} T_END;

	return fstorage;
}

static int sieve_file_storage_is_singular
(struct sieve_storage *storage)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	struct stat st;

	/* Stat the file */
	if ( lstat(fstorage->active_path, &st) != 0 ) {
		if ( errno != ENOENT ) {
			sieve_storage_set_critical(storage,
				"Failed to stat active sieve script symlink (%s): %m.",
				fstorage->active_path);
			return -1;
		}
		return 0;
	}

	if ( S_ISLNK( st.st_mode ) )
		return 0;
	if ( !S_ISREG( st.st_mode ) ) {
		sieve_storage_set_critical(storage,
			"Active sieve script file '%s' is no symlink nor a regular file.",
			fstorage->active_path);
		return -1;
	}
	return 1;
}


/*
 *
 */

static int sieve_file_storage_get_last_change
(struct sieve_storage *storage, time_t *last_change_r)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	struct stat st;

	if ( fstorage->prev_mtime == (time_t)-1 ) {
		/* Get the storage mtime before we modify it ourself */
		if ( stat(fstorage->path, &st) < 0 ) {
			if ( errno != ENOENT ) {
				sieve_storage_sys_error(storage,
					"stat(%s) failed: %m", fstorage->path);
				return -1;
			}
			st.st_mtime = 0;
		}

		fstorage->prev_mtime = st.st_mtime;
	}
		
	if ( last_change_r != NULL )
		*last_change_r = fstorage->prev_mtime;
	return 0;
}

int sieve_file_storage_pre_modify
(struct sieve_storage *storage)
{
	i_assert( (storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0 );

	return sieve_storage_get_last_change(storage, NULL);
}

static void sieve_file_storage_set_modified
(struct sieve_storage *storage, time_t mtime)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	struct utimbuf times;
	time_t cur_mtime;

	if ( mtime != (time_t)-1 ) {
		if ( sieve_storage_get_last_change(storage, &cur_mtime) >= 0 &&
			cur_mtime > mtime )
			return;
	} else {
		mtime = ioloop_time;
	}

	times.actime = mtime;
	times.modtime = mtime;
	if ( utime(fstorage->path, &times) < 0 ) {
		switch ( errno ) {
		case ENOENT:
			break;
		case EACCES:
			sieve_storage_sys_error(storage,
				"%s", eacces_error_get("utime", fstorage->path));
			break;
		default:
			sieve_storage_sys_error(storage,
				"utime(%s) failed: %m", fstorage->path);
		}
	} else {
		fstorage->prev_mtime = mtime;
	}
}

/*
 * Script access
 */

static struct sieve_script *sieve_file_storage_get_script
(struct sieve_storage *storage, const char *name)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	struct sieve_file_script *fscript;

	T_BEGIN {
		fscript = sieve_file_script_init_from_name(fstorage, name);
	} T_END;

	return &fscript->script;
}

/*
 * Driver definition
 */

const struct sieve_storage sieve_file_storage = {
	.driver_name = SIEVE_FILE_STORAGE_DRIVER_NAME,
	.allows_synchronization = TRUE,
	.v = {
		.alloc = sieve_file_storage_alloc,
		.init = sieve_file_storage_init,

		.get_last_change = sieve_file_storage_get_last_change,
		.set_modified = sieve_file_storage_set_modified,

		.is_singular = sieve_file_storage_is_singular,

		.get_script = sieve_file_storage_get_script,

		.get_script_sequence = sieve_file_storage_get_script_sequence,
		.script_sequence_next = sieve_file_script_sequence_next,
		.script_sequence_destroy = sieve_file_script_sequence_destroy,

		.active_script_get_name = sieve_file_storage_active_script_get_name,
		.active_script_open = sieve_file_storage_active_script_open,
		.deactivate = sieve_file_storage_deactivate,
		.active_script_get_last_change =
			sieve_file_storage_active_script_get_last_change,

		.list_init = sieve_file_storage_list_init,
		.list_next = sieve_file_storage_list_next,
		.list_deinit = sieve_file_storage_list_deinit,

		.save_init = sieve_file_storage_save_init,
		.save_continue = sieve_file_storage_save_continue,
		.save_finish = sieve_file_storage_save_finish,
		.save_get_tempscript = sieve_file_storage_save_get_tempscript,
		.save_cancel = sieve_file_storage_save_cancel,
		.save_commit = sieve_file_storage_save_commit,
		.save_as_active = sieve_file_storage_save_as_active,

		.quota_havespace = sieve_file_storage_quota_havespace
	}
};
