/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "path-util.h"
#include "home-expand.h"
#include "ioloop.h"
#include "mkdir-parents.h"
#include "eacces-error.h"
#include "unlink-old-files.h"
#include "settings.h"
#include "mail-storage-private.h"

#include "sieve.h"
#include "sieve-common.h"
#include "sieve-error-private.h"

#include "sieve-file-storage.h"

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <utime.h>
#include <sys/time.h>


#define MAX_DIR_CREATE_MODE 0770

/*
 * Utility
 */

const char *
sieve_file_storage_path_extend(struct sieve_file_storage *fstorage,
			       const char *filename)
{
	const char *path = fstorage->path;

	if (path[strlen(path)-1] == '/')
		return t_strconcat(path, filename, NULL);

	return t_strconcat(path, "/", filename , NULL);
}

/*
 *
 */

static int
sieve_file_storage_stat(struct sieve_file_storage *fstorage, const char *path)
{
	struct sieve_storage *storage = &fstorage->storage;
	struct stat st;
	const char *abspath, *error;

	if (lstat(path, &st) == 0) {
		fstorage->lnk_st = st;

		if (!S_ISLNK(st.st_mode) || stat(path, &st) == 0) {
			fstorage->st = st;
			return 0;
		}
	}

	switch (errno) {
	case ENOENT:
		if (t_abspath(path, &abspath, &error) < 0) {
			sieve_storage_set_critical(
				storage, "t_abspath(%s) failed: %s", path,
				error);
			break;
		}
		e_debug(storage->event, "Storage path '%s' not found", abspath);
		sieve_storage_set_internal_error(storage); // should be overriden
		storage->error_code = SIEVE_ERROR_NOT_FOUND;
		break;
	case EACCES:
		sieve_storage_set_critical(
			storage, "Failed to stat sieve storage path: %s",
			eacces_error_get("stat", path));
		storage->error_code = SIEVE_ERROR_NO_PERMISSION;
		break;
	default:
		sieve_storage_set_critical(
			storage, "Failed to stat sieve storage path: "
			"stat(%s) failed: %m", path);
		break;
	}

	return -1;
}

static const char *
sieve_storage_get_relative_link_path(const char *active_path,
				     const char *storage_dir)
{
	const char *link_path, *p;
	size_t pathlen;

	/* Determine to what extent the sieve storage and active script paths
	   match up. This enables the managed symlink to be short and the sieve
	   storages can be moved around without trouble (if the active script
	   path is common to the script storage).
	 */
	p = strrchr(active_path, '/');
	if (p == NULL) {
		link_path = storage_dir;
	} else {
		pathlen = p - active_path;

		if (strncmp(storage_dir, active_path, pathlen) == 0 &&
		    (storage_dir[pathlen] == '/' ||
		     storage_dir[pathlen] == '\0')) {
			if (storage_dir[pathlen] == '\0')
				link_path = "";
			else
				link_path = storage_dir + pathlen + 1;
		} else {
			link_path = storage_dir;
		}
	}

	/* Add trailing '/' when link path is not empty */
	pathlen = strlen(link_path);
	if (pathlen != 0 && link_path[pathlen-1] != '/')
		return t_strconcat(link_path, "/", NULL);

	return t_strdup(link_path);
}

static int
mkdir_verify(struct sieve_storage *storage, const char *dir,
	     mode_t mode, gid_t gid, const char *gid_origin)
{
	struct stat st;

	if (stat(dir, &st) == 0)
		return 0;

	if (errno == EACCES) {
		sieve_storage_set_critical(
			storage, "mkdir_verify: %s",
			eacces_error_get("stat", dir));
		return -1;
	} else if (errno != ENOENT) {
		sieve_storage_set_critical(
			storage, "mkdir_verify: "
			"stat(%s) failed: %m", dir);
		return -1;
	}

	if (mkdir_parents_chgrp(dir, mode, gid, gid_origin) == 0) {
		e_debug(storage->event, "Created storage directory %s", dir);
		return 0;
	}

	switch (errno) {
	case EEXIST:
		return 0;
	case ENOENT:
		sieve_storage_set_critical(storage,
			"Storage was deleted while it was being created");
		break;
	case EACCES:
		sieve_storage_set_critical(storage, "%s",
			eacces_error_get_creating("mkdir_parents_chgrp", dir));
		break;
	default:
		sieve_storage_set_critical(storage,
			"mkdir_parents_chgrp(%s) failed: %m", dir);
		break;
	}

	return -1;
}

static int check_tmp(struct sieve_storage *storage, const char *path)
{
	struct stat st;

	/* If tmp/ directory exists, we need to clean it up once in a while */
	if (stat(path, &st) < 0) {
		if (errno == ENOENT)
			return 0;
		if (errno == EACCES) {
			sieve_storage_set_critical(storage,
				"check_tmp: %s",
				eacces_error_get("stat", path));
			return -1;
		}
		sieve_storage_set_critical(storage,
			"check_tmp: stat(%s) failed: %m", path);
		return -1;
	}

	if (st.st_atime > st.st_ctime + SIEVE_FILE_STORAGE_TMP_DELETE_SECS) {
		/* The directory should be empty. we won't do anything
		   until ctime changes. */
	} else if (st.st_atime <
		   (ioloop_time - SIEVE_FILE_STORAGE_TMP_SCAN_SECS)) {
		/* Time to scan */
		(void)unlink_old_files(path, "",
				       (ioloop_time -
					SIEVE_FILE_STORAGE_TMP_DELETE_SECS));
	}
	return 1;
}

static struct sieve_storage *sieve_file_storage_alloc(void)
{
	struct sieve_file_storage *fstorage;
	pool_t pool;

	pool = pool_alloconly_create("sieve_file_storage", 2048);
	fstorage = p_new(pool, struct sieve_file_storage, 1);
	fstorage->storage = sieve_file_storage;
	fstorage->storage.pool = pool;

	return &fstorage->storage;
}

static int
sieve_file_storage_get_full_path(struct sieve_file_storage *fstorage,
				 const char **storage_path)
{
	struct sieve_storage *storage = &fstorage->storage;
	const char *path = *storage_path;

	if (sieve_storage_get_full_path(storage, path, storage_path) < 0) {
		sieve_storage_set_critical(
			storage,
			"Sieve storage path '%s' is relative to home directory, "
			"but home directory is not available.", path);
		return -1;
	}
	return 0;
}

static int
sieve_file_storage_get_full_active_path(struct sieve_file_storage *fstorage,
					const char **active_path)
{
	struct sieve_storage *storage = &fstorage->storage;
	const char *path = *active_path;

	if (sieve_storage_get_full_path(storage, path, active_path) < 0) {
		sieve_storage_set_critical(
			storage,
			"Sieve storage active script path '%s' is relative to home directory, "
			"but home directory is not available.", path);
		return -1;
	}
	return 0;
}

static int
sieve_file_storage_init_common(struct sieve_file_storage *fstorage,
			       const char *active_path,
			       const char *storage_path, bool exists)
{
	struct sieve_storage *storage = &fstorage->storage;
	const char *tmp_dir, *link_path, *active_fname, *storage_dir, *error;
	int ret;

	i_assert(storage_path != NULL || active_path != NULL);

	fstorage->prev_mtime = (time_t)-1;

	/* Get active script path */

	if (sieve_file_storage_get_full_active_path(fstorage, &active_path) < 0)
		return -1;

	/* Get the filename for the active script link */

	active_fname = NULL;
	if (active_path != NULL && *active_path != '\0') {
		const char *active_dir;

		active_fname = strrchr(active_path, '/');
		if (active_fname == NULL) {
			active_fname = active_path;
			active_dir = "";
		} else {
			active_dir = t_strdup_until(active_path, active_fname);
			active_fname++;
		}

		if (*active_fname == '\0') {
			/* Link cannot be just a path ending in '/' */
			sieve_storage_set_critical(
				storage,
				"Path to %sscript must include the filename (path=%s)",
				(storage_path != NULL ? "active link/" : ""),
				active_path);
			return -1;
		}

		if (t_realpath(active_dir, &active_dir, &error) < 0) {
			if (errno != ENOENT) {
				sieve_storage_set_critical(storage,
					"Failed to normalize active script directory "
					"(path=%s): %s", active_dir, error);
				return -1;
			}
			e_debug(storage->event,
				"Failed to normalize active script directory "
				"(path=%s): "
				"Part of the path does not exist (yet)",
				active_dir);
		} else {
			active_path = t_abspath_to(active_fname, active_dir);
		}

		e_debug(storage->event, "Using %sSieve script path: %s",
			(storage_path != NULL ? "active " : ""), active_path);

		fstorage->active_path = p_strdup(storage->pool, active_path);
		fstorage->active_fname = p_strdup(storage->pool, active_fname);
	}

	/* Determine storage path */

	storage_dir = storage_path;
	if (storage_path != NULL && *storage_path != '\0') {
		e_debug(storage->event, "Using script storage path: %s",
			storage_path);
		fstorage->is_file = FALSE;
	} else {
		if ((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0) {
			sieve_storage_set_critical(storage,
				"Storage path cannot be empty for write access");
			return -1;
		}

		storage_path = active_path;
		fstorage->is_file = TRUE;
	}

	i_assert(storage_path != NULL);

	/* Prepare for write access */

	if ((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0) {
		mode_t dir_create_mode, file_create_mode;
		gid_t file_create_gid;
		const char *file_create_gid_origin;

		/* Use safe permission defaults */
		file_create_mode = 0600;
		dir_create_mode = 0700;
		file_create_gid = (gid_t)-1;
		file_create_gid_origin = "defaults";

		/* Get actual permissions */
		if (exists) {
			file_create_mode = (fstorage->st.st_mode & 0666) | 0600;
			dir_create_mode = (fstorage->st.st_mode & 0777) | 0700;
			file_create_gid_origin = storage_dir;

			if (!S_ISDIR(fstorage->st.st_mode)) {
				/* We're getting permissions from a file.
				   Apply +x modes as necessary. */
				dir_create_mode = mkdir_get_executable_mode(
					dir_create_mode);
			}

			if (S_ISDIR(fstorage->st.st_mode) &&
			    (fstorage->st.st_mode & S_ISGID) != 0) {
				/* Directory's GID is used automatically for new
				   files */
				file_create_gid = (gid_t)-1;
			} else if (((fstorage->st.st_mode & 0070) >> 3) ==
				   (fstorage->st.st_mode & 0007)) {
				/* Group has same permissions as world, so don't
				   bother changing it */
				file_create_gid = (gid_t)-1;
			} else if (getegid() == fstorage->st.st_gid) {
				/* Using our own gid, no need to change it */
				file_create_gid = (gid_t)-1;
			} else {
				file_create_gid = fstorage->st.st_gid;
			}
		}

		e_debug(storage->event,
			"Using permissions from %s: mode=0%o gid=%ld",
			file_create_gid_origin, (int)dir_create_mode,
			file_create_gid == (gid_t)-1 ?
				-1L : (long)file_create_gid);

		/* Ensure sieve local directory structure exists (full
		   autocreate): This currently only consists of a ./tmp
		   directory.
		 */

		tmp_dir = t_strconcat(storage_path, "/tmp", NULL);

		/* Try to find and clean up tmp dir */
		ret = check_tmp(storage, tmp_dir);
		if (ret < 0)
			return -1;

		/* Auto-create if necessary */
		if (ret == 0 && mkdir_verify(storage, tmp_dir, dir_create_mode,
					     file_create_gid,
					     file_create_gid_origin) < 0)
			return -1;

		fstorage->dir_create_mode = dir_create_mode;
		fstorage->file_create_mode = file_create_mode;
		fstorage->file_create_gid = file_create_gid;
	}

	if (!exists && sieve_file_storage_stat(fstorage, storage_path) < 0)
		return -1;

	if (!fstorage->is_file) {
		if (t_realpath(storage_path, &storage_path, &error) < 0) {
			sieve_storage_set_critical(storage,
				"Failed to normalize storage path (path=%s): %s",
				storage_path, error);
			return -1;
		}
		if (active_path != NULL && *active_path != '\0') {
			/* Get the path to be prefixed to the script name in the
			   symlink pointing to the active script.
			 */
			link_path = sieve_storage_get_relative_link_path(
				fstorage->active_path, storage_path);

			e_debug(storage->event,
				"Relative path to sieve storage in active link: %s",
				link_path);

			fstorage->link_path =
				p_strdup(storage->pool, link_path);
		}
	}

	fstorage->path = p_strdup(storage->pool, storage_path);
	return 0;
}

static int
sieve_file_storage_init_from_settings(
	struct sieve_file_storage *fstorage,
	const struct sieve_file_storage_settings *set)
{
	struct sieve_storage *storage = &fstorage->storage;
	const char *storage_path = set->script_path;
	const char *active_path = set->script_active_path;
	bool exists = FALSE;

	/* Get full storage path */

	if (sieve_file_storage_get_full_path(fstorage, &storage_path) < 0)
		return -1;

	/* Stat storage directory */

	bool is_personal = sieve_storage_is_personal(storage);

	if (storage_path != NULL && *storage_path != '\0') {
		if (sieve_file_storage_stat(fstorage, storage_path) < 0) {
			if (!is_personal ||
			    storage->error_code != SIEVE_ERROR_NOT_FOUND)
				return -1;
			if ((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) == 0) {
				/* For backwards compatibility, recognize when
				   storage directory does not exist while active
				   script exists and is a regular file.
				 */
				if (active_path == NULL || *active_path == '\0')
					return -1;
				if (sieve_file_storage_get_full_active_path(
					fstorage, &active_path) < 0)
					return -1;
				if (sieve_file_storage_stat(fstorage,
							    active_path) < 0)
					return -1;
				if (!S_ISREG(fstorage->lnk_st.st_mode))
					return -1;
				e_debug(storage->event,
					"Sieve storage path '%s' not found, "
					"but the active script '%s' is a regular file, "
					"so this is used for backwards compatibility.",
					storage_path, active_path);
				storage_path = NULL;
			}
		} else {
			exists = TRUE;

			if (!S_ISDIR(fstorage->st.st_mode)) {
				if ((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0) {
					sieve_storage_set_critical(storage,
						"Sieve storage path '%s' is not a directory, "
						"but it is to be opened for write access", storage_path);
					return -1;
				}
				if (active_path != NULL && *active_path != '\0') {
					e_warning(storage->event,
						  "Explicitly specified active script path '%s' is ignored; "
						  "storage path '%s' is not a directory",
						  active_path, storage_path);
				}
				active_path = storage_path;
				storage_path = NULL;
			}
		}
	}

	if ((active_path == NULL || *active_path == '\0') &&
	    (is_personal ||
	     (storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0)) {
		e_debug(storage->event,
			"Active script path is unconfigured; "
			"using default (path=%s)",
			SIEVE_FILE_DEFAULT_ACTIVE_PATH);
		active_path = SIEVE_FILE_DEFAULT_ACTIVE_PATH;
	}

	return sieve_file_storage_init_common(fstorage, active_path,
					      storage_path, exists);
}

static int sieve_file_storage_init(struct sieve_storage *storage)
{
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	const struct sieve_file_storage_settings *fstorage_set;
	const char *error;
	int ret;

	if (settings_get(storage->event,
			 &sieve_file_storage_setting_parser_info, 0,
			 &fstorage_set, &error) < 0) {
		e_error(storage->event, "%s", error);
		sieve_storage_set_internal_error(storage);
		return -1;
	}

	ret = sieve_file_storage_init_from_settings(fstorage, fstorage_set);
	settings_free(fstorage_set);
	if (ret < 0)
		return -1;

	return ret;
}

static int
sieve_file_storage_do_autodetect(
	struct sieve_instance *svinst, struct event *event, const char *cause,
	const struct sieve_storage_settings *storage_set,
	const struct sieve_file_storage_settings *fstorage_set,
	enum sieve_storage_flags flags, struct sieve_storage **storage_r,
	enum sieve_error *error_code_r, const char **error_r)
{
	const char *home = sieve_environment_get_homedir(svinst);
	int mode = ((flags & SIEVE_STORAGE_FLAG_READWRITE) != 0 ?
		    R_OK|W_OK|X_OK : R_OK|X_OK);
	const char *storage_path = fstorage_set->script_path;

	if (storage_path == NULL || *storage_path == '\0') {
		/* We'll need to figure out the storage location ourself.
		   It's $HOME/sieve or /sieve when (presumed to be) chrooted.
		 */
		if (home != NULL && *home != '\0') {
			/* Use default ~/sieve */
			e_debug(event, "Use home (%s)", home);
			storage_path = t_strconcat(home, "/sieve", NULL);
		} else {
			e_debug(event, "HOME is not set");

			if (access("/sieve", mode) == 0) {
				storage_path = "/sieve";
				e_debug(event, "Directory '/sieve' exists, "
					"assuming chroot");
			}
		}
	}

	if ((storage_path == NULL || *storage_path == '\0') &&
	    (flags & SIEVE_STORAGE_FLAG_READWRITE) != 0) {
		e_error(event,
			"Could not find storage root directory for write access; "
			"path was left unconfigured and autodetection failed");
		sieve_error_create_internal(error_code_r, error_r);
		return -1;
	}

	struct sieve_storage *storage;
	struct sieve_file_storage *fstorage;
	const char *active_path = NULL;
	bool exists = FALSE;
	int ret;

	ret = sieve_storage_alloc_with_settings(svinst, event,
						&sieve_file_storage, cause,
						storage_set, flags, &storage,
						error_code_r, error_r);
	if (ret < 0)
		return -1;

	event = storage->event;
	fstorage = container_of(storage, struct sieve_file_storage, storage);

	/* Determine what we have found so far */
	bool tried_active = FALSE;
	while (!tried_active) {
		if (storage_path == NULL || *storage_path == '\0') {
			storage_path = fstorage_set->script_active_path;
			if (storage_path == NULL || *storage_path == '\0')
				storage_path = SIEVE_FILE_DEFAULT_ACTIVE_PATH;
			tried_active = TRUE;
		}
		e_debug(event, "Checking storage path %s", storage_path);

		/* Get full storage path */
		if (sieve_file_storage_get_full_path(fstorage,
						     &storage_path) < 0) {
			*error_code_r = storage->error_code;
			*error_r = t_strdup(storage->error);
			sieve_storage_unref(&storage);
			return -1;
		}

		/* Got something: stat it */
		ret = sieve_file_storage_stat(fstorage, storage_path);
		if (ret < 0) {
			if (storage->error_code != SIEVE_ERROR_NOT_FOUND) {
				/* Error */
				*error_code_r = storage->error_code;
				*error_r = t_strdup(storage->error);
				sieve_storage_unref(&storage);
				return -1;
			}
			if ((storage->flags &
			     SIEVE_STORAGE_FLAG_READWRITE) != 0)
				break;
		}
		if (ret == 0)
			break;
		storage_path = NULL;
	}

	if (storage_path == NULL || *storage_path == '\0') {
		sieve_storage_unref(&storage);
		return 0;
	}

	if (storage->error_code != SIEVE_ERROR_NONE) {
		/* Not found */
	} else if (S_ISDIR(fstorage->st.st_mode)) {
		if (tried_active) {
			e_error(event,
				"Active script path '%s' is a directory",
				storage_path);
			sieve_error_create_internal(error_code_r, error_r);
			sieve_storage_unref(&storage);
			return -1;
		}

		/* Success */
		exists = TRUE;
		active_path = fstorage_set->script_active_path;
	} else if ((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) == 0) {
		exists = TRUE;
		active_path = storage_path;
		storage_path = NULL;
	}

	if (active_path == NULL || *active_path == '\0') {
		e_debug(event, "Active script path is unconfigured; "
			"using default (path=%s)",
			SIEVE_FILE_DEFAULT_ACTIVE_PATH);
		active_path = SIEVE_FILE_DEFAULT_ACTIVE_PATH;
	}

	if (sieve_file_storage_init_common(fstorage, active_path, storage_path,
					   exists) < 0) {
		*error_code_r = storage->error_code;
		*error_r = t_strdup(storage->error);
		sieve_storage_unref(&storage);
		return -1;
	}

	*storage_r = storage;
	return 1;
}

static int
sieve_file_storage_autodetect(struct sieve_instance *svinst,
			      struct event *event, const char *cause,
			      const struct sieve_storage_settings *storage_set,
			      enum sieve_storage_flags flags,
			      struct sieve_storage **storage_r,
			      enum sieve_error *error_code_r,
			      const char **error_r)
{
	const struct sieve_file_storage_settings *fstorage_set;
	int ret;

	if (!sieve_storage_settings_match_script_type(
		storage_set, SIEVE_STORAGE_TYPE_PERSONAL))
		return 0;

	e_debug(event, "Performing auto-detection");

	const char *error;

	if (settings_get(event, &sieve_file_storage_setting_parser_info, 0,
			 &fstorage_set, &error) < 0) {
		e_error(event, "%s", error);
		sieve_error_create_internal(error_code_r, error_r);
		return -1;
	}

	ret = sieve_file_storage_do_autodetect(
		svinst, event, cause, storage_set, fstorage_set, flags,
		storage_r, error_code_r, error_r);

	settings_free(fstorage_set);
	return ret;
}

int sieve_file_storage_init_from_path(struct sieve_instance *svinst,
				      const char *cause,
				      const char *script_type,
				      const char *storage_name,
				      const char *path,
				      enum sieve_storage_flags flags,
				      struct sieve_file_storage **fstorage_r,
				      enum sieve_error *error_code_r,
				      const char **error_r)
{
	struct sieve_storage *storage;
	struct sieve_file_storage *fstorage;
	int ret;

	i_assert(path != NULL);

	*fstorage_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	ret = sieve_storage_alloc(svinst, svinst->event, &sieve_file_storage,
				  cause, script_type, storage_name,
				  sieve_script_file_get_scriptname(path),
				  flags, &storage, error_code_r, error_r);
	if (ret < 0)
		return -1;
	fstorage = container_of(storage, struct sieve_file_storage, storage);

	T_BEGIN {
		ret = sieve_file_storage_init_common(fstorage, path, NULL,
						     FALSE);
	} T_END;
	if (ret < 0) {
		*error_code_r = storage->error_code;
		*error_r = t_strdup(storage->error);
		sieve_storage_unref(&storage);
		return -1;
	}
	*fstorage_r = fstorage;
	return 0;
}

static int sieve_file_storage_is_singular(struct sieve_storage *storage)
{
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	struct stat st;

	if (fstorage->active_path == NULL)
		return 1;

	/* Stat the file */
	if (lstat(fstorage->active_path, &st) != 0) {
		if (errno != ENOENT) {
			sieve_storage_set_critical(storage,
				"Failed to stat active sieve script symlink (%s): %m.",
				fstorage->active_path);
			return -1;
		}
		return 0;
	}

	if (S_ISLNK(st.st_mode))
		return 0;
	if (!S_ISREG(st.st_mode)) {
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

static int
sieve_file_storage_get_last_change(struct sieve_storage *storage,
				   time_t *last_change_r)
{
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	struct stat st;

	if (fstorage->prev_mtime == (time_t)-1) {
		/* Get the storage mtime before we modify it ourself */
		if (stat(fstorage->path, &st) < 0) {
			if (errno != ENOENT) {
				e_error(storage->event,
					"stat(%s) failed: %m",
					fstorage->path);
				return -1;
			}
			st.st_mtime = 0;
		}

		fstorage->prev_mtime = st.st_mtime;
	}

	if (last_change_r != NULL)
		*last_change_r = fstorage->prev_mtime;
	return 0;
}

int sieve_file_storage_pre_modify(struct sieve_storage *storage)
{
	i_assert((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0);

	return sieve_storage_get_last_change(storage, NULL);
}

static void
sieve_file_storage_set_modified(struct sieve_storage *storage, time_t mtime)
{
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	struct utimbuf times;
	time_t cur_mtime;

	if (mtime != (time_t)-1) {
		if (sieve_storage_get_last_change(storage, &cur_mtime) >= 0 &&
		    cur_mtime > mtime)
			return;
	} else {
		mtime = ioloop_time;
	}

	times.actime = mtime;
	times.modtime = mtime;
	if (utime(fstorage->path, &times) < 0) {
		switch (errno) {
		case ENOENT:
			break;
		case EACCES:
			e_error(storage->event, "%s",
				eacces_error_get("utime", fstorage->path));
			break;
		default:
			e_error(storage->event,
				"utime(%s) failed: %m", fstorage->path);
		}
	} else {
		fstorage->prev_mtime = mtime;
	}
}

/*
 * Script access
 */

static int
sieve_file_storage_get_script(struct sieve_storage *storage, const char *name,
			      struct sieve_script **script_r)
{
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	struct sieve_file_script *fscript;
	int ret;

	T_BEGIN {
		ret = sieve_file_script_init_from_name(fstorage, name,
						       &fscript);
	} T_END;

	if (ret < 0)
		return -1;
	i_assert(fscript != NULL);
	*script_r = &fscript->script;
	return 0;
}

/*
 * Driver definition
 */

const struct sieve_storage sieve_file_storage = {
	.driver_name = SIEVE_FILE_STORAGE_DRIVER_NAME,
	.version = 0,
	.allows_synchronization = TRUE,
	.v = {
		.alloc = sieve_file_storage_alloc,
		.init = sieve_file_storage_init,

		.autodetect = sieve_file_storage_autodetect,

		.get_last_change = sieve_file_storage_get_last_change,
		.set_modified = sieve_file_storage_set_modified,

		.is_singular = sieve_file_storage_is_singular,

		.get_script = sieve_file_storage_get_script,

		.script_sequence_init = sieve_file_script_sequence_init,
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

		.save_alloc = sieve_file_storage_save_alloc,
		.save_init = sieve_file_storage_save_init,
		.save_continue = sieve_file_storage_save_continue,
		.save_finish = sieve_file_storage_save_finish,
		.save_get_tempscript = sieve_file_storage_save_get_tempscript,
		.save_cancel = sieve_file_storage_save_cancel,
		.save_commit = sieve_file_storage_save_commit,
		.save_as = sieve_file_storage_save_as,
		.save_as_active = sieve_file_storage_save_as_active,

		.quota_havespace = sieve_file_storage_quota_havespace,
	},
};
