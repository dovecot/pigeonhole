/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "eacces-error.h"

#include "sieve-common.h"
#include "sieve-script-private.h"

#include "sieve-file-storage.h"

#include <stdio.h>
#include <dirent.h>

struct sieve_file_list_context {
	struct sieve_storage_list_context context;
	pool_t pool;

	const char *active;
	const char *dir;
	DIR *dirp;
};

struct sieve_storage_list_context *
sieve_file_storage_list_init(struct sieve_storage *storage)
{
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	struct sieve_file_list_context *flctx;
	const char *active = NULL;
	pool_t pool;
	DIR *dirp;

	/* Open the directory */
	dirp = opendir(fstorage->path);
	if (dirp == NULL) {
		switch (errno) {
		case ENOENT:
			sieve_storage_set_error(
				storage, SIEVE_ERROR_NOT_FOUND,
				"Script storage not found");
			break;
		case EACCES:
			sieve_storage_set_error(
				storage, SIEVE_ERROR_NO_PERMISSION,
				"Script storage not accessible");
			e_error(storage->event, "Failed to list scripts: %s",
				eacces_error_get("opendir", fstorage->path));
			break;
		default:
			sieve_storage_set_critical(
				storage, "Failed to list scripts: "
				"opendir(%s) failed: %m", fstorage->path);
			break;
		}
		return NULL;
	}

	T_BEGIN {
		/* Get the name of the active script */
		if (sieve_file_storage_active_script_get_file(
			fstorage, &active) < 0) {
			flctx = NULL;
		} else {
			pool = pool_alloconly_create("sieve_file_list_context",
						     1024);
			flctx = p_new(pool, struct sieve_file_list_context, 1);
			flctx->pool = pool;
			flctx->dirp = dirp;
			flctx->active = (active != NULL ?
					 p_strdup(pool, active) : NULL);
		}
	} T_END;

	if (flctx == NULL) {
		if (closedir(dirp) < 0) {
			e_error(storage->event,
				"closedir(%s) failed: %m", fstorage->path);
		}
		return NULL;
	}
	return &flctx->context;
}

const char *
sieve_file_storage_list_next(struct sieve_storage_list_context *lctx,
			     bool *active)
{
	struct sieve_file_list_context *flctx =
		container_of(lctx, struct sieve_file_list_context, context);
	const struct sieve_file_storage *fstorage =
		container_of(lctx->storage, struct sieve_file_storage, storage);
	struct dirent *dp;
	const char *scriptname;

	*active = FALSE;

	for (;;) {
		if ((dp = readdir(flctx->dirp)) == NULL)
			return NULL;

		scriptname = sieve_script_file_get_scriptname(dp->d_name);
		if (scriptname != NULL) {
			/* Don't list our active sieve script link if the link
			   resides in the script dir (generally a bad idea).
			 */
			i_assert( fstorage->link_path != NULL );
			if (*(fstorage->link_path) == '\0' &&
			    strcmp(fstorage->active_fname, dp->d_name) == 0)
				continue;

			break;
		}
	}

	if (flctx->active != NULL && strcmp(dp->d_name, flctx->active) == 0) {
		*active = TRUE;
		flctx->active = NULL;
	}

	return scriptname;
}

int sieve_file_storage_list_deinit(struct sieve_storage_list_context *lctx)
{
	struct sieve_file_list_context *flctx =
		container_of(lctx, struct sieve_file_list_context, context);
	const struct sieve_file_storage *fstorage =
		container_of(lctx->storage, struct sieve_file_storage, storage);

	if (closedir(flctx->dirp) < 0) {
		e_error(lctx->storage->event,
			"closedir(%s) failed: %m", fstorage->path);
	}

	pool_unref(&flctx->pool);

	// FIXME: return error here if something went wrong during listing
	return 0;
}
