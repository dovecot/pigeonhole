/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

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

struct sieve_storage_list_context *sieve_file_storage_list_init
(struct sieve_storage *storage)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	struct sieve_file_list_context *flctx;
	const char *active = NULL;
	pool_t pool;
	DIR *dirp;

	/* Open the directory */
	if ( (dirp = opendir(fstorage->path)) == NULL ) {
		sieve_storage_set_critical(storage,
			"Failed to list scripts: "
			"opendir(%s) failed: %m", fstorage->path);
		return NULL;
	}

	T_BEGIN {
		/* Get the name of the active script */
		if ( sieve_file_storage_active_script_get_file(fstorage, &active) < 0) {
			flctx = NULL;
		} else {
			pool = pool_alloconly_create("sieve_file_list_context", 1024);
			flctx = p_new(pool, struct sieve_file_list_context, 1);
			flctx->pool = pool;
			flctx->dirp = dirp;
			flctx->active = ( active != NULL ? p_strdup(pool, active) : NULL );
		}
	} T_END;

	if ( flctx == NULL ) {
		if ( closedir(dirp) < 0) {
			sieve_storage_sys_error(storage,
				"closedir(%s) failed: %m", fstorage->path);	
		}
		return NULL;
	}
	return &flctx->context;
}

const char *sieve_file_storage_list_next
(struct sieve_storage_list_context *ctx, bool *active)
{
	struct sieve_file_list_context *flctx =
		(struct sieve_file_list_context *)ctx;
	const struct sieve_file_storage *fstorage =
		(const struct sieve_file_storage *)ctx->storage;
	struct dirent *dp;
	const char *scriptname;

	*active = FALSE;

	for (;;) {
		if ( (dp = readdir(flctx->dirp)) == NULL )
			return NULL;

		scriptname = sieve_script_file_get_scriptname(dp->d_name);
		if (scriptname != NULL ) {
			/* Don't list our active sieve script link if the link
			 * resides in the script dir (generally a bad idea).
			 */
			i_assert( fstorage->link_path != NULL );
			if ( *(fstorage->link_path) == '\0' &&
				strcmp(fstorage->active_fname, dp->d_name) == 0 )
				continue;

			break;
		}
	}

	if ( flctx->active != NULL && strcmp(dp->d_name, flctx->active) == 0 ) {
		*active = TRUE;
		flctx->active = NULL;
	}

	return scriptname;
}

int sieve_file_storage_list_deinit(struct sieve_storage_list_context *lctx)
{
	struct sieve_file_list_context *flctx =
		(struct sieve_file_list_context *)lctx;
	const struct sieve_file_storage *fstorage =
		(const struct sieve_file_storage *)lctx->storage;

	if (closedir(flctx->dirp) < 0) {
		sieve_storage_sys_error(lctx->storage,
			"closedir(%s) failed: %m", fstorage->path);
	}

	pool_unref(&flctx->pool);

	// FIXME: return error here if something went wrong during listing
	return 0;
}




