/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "sieve.h"
#include "sieve-script.h"

#include "sieve-file-storage.h"

#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

int sieve_file_storage_quota_havespace
(struct sieve_storage *storage, const char *scriptname, size_t size,
	enum sieve_storage_quota *quota_r, uint64_t *limit_r)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	struct dirent *dp;
	DIR *dirp;
	uint64_t script_count = 1;
	uint64_t script_storage = size;
	int result = 1;

	/* Open the directory */
	if ( (dirp = opendir(fstorage->path)) == NULL ) {
		sieve_storage_set_critical(storage,
			"quota: opendir(%s) failed: %m", fstorage->path);
		return -1;
	}

	/* Scan all files */
	for (;;) {
		const char *name;
		bool replaced = FALSE;

		/* Read next entry */
		errno = 0;
		if ( (dp = readdir(dirp)) == NULL ) {
			if ( errno != 0 ) {
				sieve_storage_set_critical(storage,
					"quota: readdir(%s) failed: %m", fstorage->path);
				result = -1;
			}
			break;
		}

		/* Parse filename */
		name = sieve_script_file_get_scriptname(dp->d_name);

		/* Ignore non-script files */
		if ( name == NULL )
			continue;

		/* Don't list our active sieve script link if the link
		 * resides in the script dir (generally a bad idea).
		 */
		i_assert( fstorage->link_path != NULL );
		if ( *(fstorage->link_path) == '\0' &&
			strcmp(fstorage->active_fname, dp->d_name) == 0 )
			continue;

		if ( strcmp(name, scriptname) == 0 )
			replaced = TRUE;

		/* Check count quota if necessary */
		if ( storage->max_scripts > 0 ) {
			if ( !replaced ) {
				script_count++;

				if ( script_count > storage->max_scripts ) {
					*quota_r = SIEVE_STORAGE_QUOTA_MAXSCRIPTS;
					*limit_r = storage->max_scripts;
					result = 0;
					break;
				}
			}
		}

		/* Check storage quota if necessary */
		if ( storage->max_storage > 0 ) {
			const char *path;
			struct stat st;

			path = t_strconcat(fstorage->path, "/", dp->d_name, NULL);

			if ( stat(path, &st) < 0 ) {
				sieve_storage_sys_warning(storage,
					"quota: stat(%s) failed: %m", path);
				continue;
			}

			if ( !replaced ) {
				script_storage += st.st_size;

				if ( script_storage > storage->max_storage ) {
					*quota_r = SIEVE_STORAGE_QUOTA_MAXSTORAGE;
					*limit_r = storage->max_storage;
					result = 0;
					break;
				}
			}
		}
	}

	/* Close directory */
	if ( closedir(dirp) < 0 ) {
		sieve_storage_set_critical(storage,
			"quota: closedir(%s) failed: %m", fstorage->path);
	}
	return result;
}




