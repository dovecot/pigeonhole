/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "path-util.h"
#include "ioloop.h"
#include "hostpid.h"
#include "file-copy.h"
#include "time-util.h"

#include "sieve-file-storage.h"

#include <unistd.h>

/*
 * Symlink manipulation
 */

static int
sieve_file_storage_active_read_link(struct sieve_file_storage *fstorage,
				    const char **link_r)
{
	struct sieve_storage *storage = &fstorage->storage;
	const char *error = NULL;
	int ret;

	if (fstorage->is_file) {
		/* The storage is in fact a single script file. There is no
		   concept of an active script in this storage. */
		return 0;
	}

	ret = t_readlink(fstorage->active_path, link_r, &error);
	if (ret < 0) {
		*link_r = NULL;

		if (errno == EINVAL) {
			/* Our symlink is no symlink. Report 'no active script'.
			   Activating a script will automatically resolve this,
			   so there is no need to panic on this one.
			 */
			if ((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0 &&
			    (storage->flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) == 0) {
				e_warning(storage->event,
					  "Active sieve script symlink %s is no symlink.",
					  fstorage->active_path);
			}
			return 0;
		}

		if (errno == ENOENT) {
			/* Symlink not found */
			return 0;
		}

		/* We do need to panic otherwise */
		sieve_storage_set_critical(storage,
			"Performing t_readlink() on active sieve symlink '%s' failed: %s",
			fstorage->active_path, error);
		return -1;
	}

	/* ret is now assured to be valid, i.e. > 0 */
	return 1;
}

static const char *
sieve_file_storage_active_parse_link(struct sieve_file_storage *fstorage,
				     const char *link,
				     const char **scriptname_r)
{
	struct sieve_storage *storage = &fstorage->storage;
	const char *fname, *scriptname, *scriptpath, *link_dir;

	/* Split off directory from link path */
	fname = strrchr(fstorage->active_path, '/');
	if (fname == NULL)
		link_dir = "";
	else
		link_dir = t_strdup_until(fstorage->active_path, fname+1);

	/* Split link into path and filename */
	fname = strrchr(link, '/');
	if (fname != NULL) {
		scriptpath = t_strdup_until(link, fname+1);
		fname++;
	} else {
		scriptpath = "";
		fname = link;
	}

	/* Check the script name */
	scriptname = sieve_script_file_get_scriptname(fname);

	/* Warn if link is deemed to be invalid */
	if (scriptname == NULL) {
		e_warning(storage->event,
			  "Active Sieve script symlink %s is broken: "
			  "Invalid scriptname (points to %s).",
			  fstorage->active_path, link);
		return NULL;
	}

	/* Check whether the path is any good */
	const char *error = NULL;
	if (t_normpath_to(scriptpath, link_dir, &scriptpath, &error) < 0) {
		e_warning(storage->event,
			  "Failed to check active Sieve script symlink %s: "
			  "Failed to normalize path (points to %s): %s",
			  fstorage->active_path, scriptpath, error);
		return NULL;
	}
	if (strcmp(scriptpath, fstorage->path) != 0) {
		e_warning(storage->event,
			  "Active sieve script symlink %s is broken: "
			  "Invalid/unknown path to storage (points to %s).",
			  fstorage->active_path, scriptpath);
		return NULL;
	}

	if (scriptname_r != NULL)
		*scriptname_r = scriptname;

	return fname;
}

int sieve_file_storage_active_replace_link(struct sieve_file_storage *fstorage,
					   const char *link_path)
{
	struct sieve_storage *storage = &fstorage->storage;
	const char *active_path_new;
	struct timeval *tv, tv_now;
	int ret = 0;

	tv = &ioloop_timeval;

	for (;;) {
		/* First the new symlink is created with a different filename */
		active_path_new = t_strdup_printf(
			"%s-new.%s.P%sM%s.%s", fstorage->active_path,
			dec2str(tv->tv_sec), my_pid, dec2str(tv->tv_usec),
			my_hostname);

		ret = symlink(link_path, active_path_new);
		if (ret < 0) {
			/* If link exists we try again later */
			if (errno == EEXIST) {
				/* Wait and try again - very unlikely */
				sleep(2);
				tv = &tv_now;
				i_gettimeofday(&tv_now);
				continue;
			}

			/* Other error, critical */
			sieve_storage_set_critical(storage,
				"Creating symlink() %s to %s failed: %m",
				active_path_new, link_path);
			return -1;
		}

		/* Link created */
		break;
	}

	/* Replace the existing link. This activates the new script */
	ret = rename(active_path_new, fstorage->active_path);
	if (ret < 0) {
		/* Failed; created symlink must be deleted */
		i_unlink(active_path_new);
		sieve_storage_set_critical(storage,
			"Performing rename() %s to %s failed: %m",
			active_path_new, fstorage->active_path);
		return -1;
	}

	return 1;
}

/*
 * Active script properties
 */

int sieve_file_storage_active_script_get_file(
	struct sieve_file_storage *fstorage, const char **file_r)
{
	const char *link, *scriptfile;
	int ret;

	*file_r = NULL;

	/* Read the active link */
	ret = sieve_file_storage_active_read_link(fstorage, &link);
	if (ret <= 0)
		return ret;

	/* Parse the link */
	scriptfile = sieve_file_storage_active_parse_link(fstorage, link, NULL);

	if (scriptfile == NULL) {
		/* Obviously, someone has been playing with our symlink:
		   ignore this situation and report 'no active script'.
		   Activation should fix this situation.
		 */
		return 0;
	}

	*file_r = scriptfile;
	return 1;
}

int sieve_file_storage_active_script_get_name(struct sieve_storage *storage,
					      const char **name_r)
{
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	const char *link;
	int ret;

	*name_r = NULL;

	/* Read the active link */
	ret = sieve_file_storage_active_read_link(fstorage, &link);
	if (ret <= 0)
		return ret;

	if (sieve_file_storage_active_parse_link(fstorage, link,
						 name_r) == NULL) {
		/* Obviously, someone has been playing with our symlink:
		   ignore this situation and report 'no active script'.
		   Activation should fix this situation.
		 */
		return 0;
	}
	return 1;
}

/*
 * Active script
 */

int sieve_file_storage_active_script_open(struct sieve_storage *storage,
					  struct sieve_script **script_r)
{
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	struct sieve_file_script *fscript;
	const char *scriptfile, *link;
	int ret;

	*script_r = NULL;
	sieve_storage_clear_error(storage);

	/* Read the active link */
	ret = sieve_file_storage_active_read_link(fstorage, &link);
	if (ret <= 0) {
		if (ret < 0)
			return -1;

		/* Try to open the active_path as a regular file */
		if (S_ISDIR(fstorage->st.st_mode)) {
			ret = sieve_file_script_open_from_path(
				fstorage, fstorage->active_path, NULL,
				&fscript);
		} else {
			ret = sieve_file_script_open_from_name(fstorage, NULL,
							       &fscript);
		}
		if (ret < 0) {
			if (storage->error_code != SIEVE_ERROR_NOT_FOUND) {
				sieve_storage_set_critical(
					storage,
					"Failed to open active path '%s' as regular file: %s",
					fstorage->active_path, storage->error);
			}
			return -1;
		}

		*script_r = &fscript->script;
		return 0;
	}

	/* Parse the link */
	scriptfile = sieve_file_storage_active_parse_link(fstorage, link, NULL);
	if (scriptfile == NULL) {
		/* Obviously someone has been playing with our symlink,
		   ignore this situation and report 'no active script'.
		   Activation should fix this situation.
		 */
		sieve_storage_set_error(storage, SIEVE_ERROR_NOT_FOUND,
					"Active script is invalid");
		return -1;
	}

	ret = sieve_file_script_open_from_path(
		fstorage, fstorage->active_path,
		sieve_script_file_get_scriptname(scriptfile), &fscript);
	if (ret < 0 && storage->error_code == SIEVE_ERROR_NOT_FOUND) {
		e_warning(storage->event,
			  "Active sieve script symlink %s points to non-existent script "
			  "(points to %s).", fstorage->active_path, link);
	}
	if (ret < 0)
		return -1;
	*script_r = &fscript->script;
	return 0;
}

int sieve_file_storage_active_script_get_last_change(
	struct sieve_storage *storage, time_t *last_change_r)
{
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	struct stat st;

	/* Try direct lstat first */
	if (lstat(fstorage->active_path, &st) == 0) {
		if (!S_ISLNK(st.st_mode)) {
			*last_change_r = st.st_mtime;
			return 0;
		}
	}
	/* Check error */
	else if (errno != ENOENT) {
		sieve_storage_set_critical(storage, "lstat(%s) failed: %m",
					   fstorage->active_path);
	}

	/* Fall back to statting storage directory */
	return sieve_storage_get_last_change(storage, last_change_r);
}

bool sieve_file_storage_active_rescue_regular(
	struct sieve_file_storage *fstorage)
{
	struct sieve_storage *storage = &fstorage->storage;
	struct stat st;

	/* Stat the file */
	if (lstat(fstorage->active_path, &st) != 0) {
		if (errno != ENOENT) {
			sieve_storage_set_critical(storage,
				"Failed to stat active sieve script symlink (%s): %m.",
				fstorage->active_path);
			return FALSE;
		}
		return TRUE;
	}

	if (S_ISLNK(st.st_mode)) {
		e_debug(storage->event,
			"Nothing to rescue %s.", fstorage->active_path);
		return TRUE; /* Nothing to rescue */
	}

	/* Only regular files can be rescued */
	if (S_ISREG(st.st_mode)) {
		const char *dstpath;
		bool result = TRUE;

 		T_BEGIN {
			dstpath = t_strconcat(
				fstorage->path, "/",
				sieve_script_file_from_name("dovecot.orig"),
				NULL);
			if (file_copy(fstorage->active_path, dstpath, TRUE) < 1) {
				sieve_storage_set_critical(storage,
					"Active sieve script file '%s' is a regular file "
					"and copying it to the script storage as '%s' failed. "
					"This needs to be fixed manually.",
					fstorage->active_path, dstpath);
				result = FALSE;
			} else {
				e_info(storage->event,
				       "Moved active sieve script file '%s' "
				       "to script storage as '%s'.",
				       fstorage->active_path, dstpath);
			}
		} T_END;

		return result;
	}

	sieve_storage_set_critical(storage,
		"Active sieve script file '%s' is no symlink nor a regular file. "
		"This needs to be fixed manually.", fstorage->active_path);
	return FALSE;
}

int sieve_file_storage_deactivate(struct sieve_storage *storage)
{
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	int ret;

	if (sieve_file_storage_pre_modify(storage) < 0)
		return -1;

	if (!sieve_file_storage_active_rescue_regular(fstorage))
		return -1;

	/* Delete the symlink, so no script is active */
	ret = unlink(fstorage->active_path);

	if (ret < 0) {
		if (errno != ENOENT) {
			sieve_storage_set_critical(storage,
				"Failed to deactivate Sieve: "
				"unlink(%s) failed: %m", fstorage->active_path);
			return -1;
		} else {
			return 0;
		}
	}
	return 1;
}
