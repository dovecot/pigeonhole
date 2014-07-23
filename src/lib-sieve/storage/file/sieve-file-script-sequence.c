/* Copyright (c) 2002-2014 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "array.h"
#include "eacces-error.h"

#include "sieve-common.h"
#include "sieve-script-private.h"

#include "sieve-file-storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

/*
 * Script sequence
 */

struct sieve_file_script_sequence {
	struct sieve_script_sequence seq;
	pool_t pool;

	ARRAY_TYPE(const_string) script_files;
	unsigned int index;
};

static int sieve_file_script_sequence_read_dir
(struct sieve_file_script_sequence *fseq, const char *path)
{
	struct sieve_storage *storage = fseq->seq.storage;
	DIR *dirp;
	int ret = 0;

	/* Open the directory */
	if ( (dirp = opendir(path)) == NULL ) {
		switch ( errno ) {
		case ENOENT:
			sieve_storage_set_error(storage,
				SIEVE_ERROR_NOT_FOUND,
				"Script sequence location not found");
			break;
		case EACCES:
			sieve_storage_set_error(storage,
				SIEVE_ERROR_NO_PERMISSION,
				"Script sequence location not accessible");
			sieve_storage_sys_error(storage,
				"Failed to open sieve sequence: "
				"%s",	eacces_error_get("stat", path));
			break;
		default:
			sieve_storage_set_critical(storage,
				"Failed to open sieve sequence: "
				"opendir(%s) failed: %m", path);
			break;
		}
		return -1;
	}

	/* Read and sort script files */
	for (;;) {
		const char *const *files;
		unsigned int count, i;
		const char *file;
		struct dirent *dp;
		struct stat st;

		errno = 0;
		if ( (dp=readdir(dirp)) == NULL )
			break;

		if ( !sieve_script_file_has_extension(dp->d_name) )
			continue;

		file = NULL;
		T_BEGIN {
			if ( path[strlen(path)-1] == '/' )
				file = t_strconcat(path, dp->d_name, NULL);
			else
				file = t_strconcat(path, "/", dp->d_name, NULL);

			if ( stat(file, &st) == 0 && S_ISREG(st.st_mode) )
				file = p_strdup(fseq->pool, file);
			else
				file = NULL;
		} T_END;

		if (file == NULL)
			continue;
		
		/* Insert into sorted array */
		files = array_get(&fseq->script_files, &count);
		for ( i = 0; i < count; i++ ) {
			if ( strcmp(file, files[i]) < 0 )
				break;
		}

		if ( i == count )
			array_append(&fseq->script_files, &file, 1);
		else
			array_insert(&fseq->script_files, i, &file, 1);
	} 

	if ( errno != 0 ) {
		sieve_storage_set_critical(storage,
			"Failed to read sequence directory: "
			"readdir(%s) failed: %m", path);
		ret = -1;
	}

	/* Close the directory */
	if ( dirp != NULL && closedir(dirp) < 0 ) {
		sieve_storage_sys_error(storage,
			"Failed to close sequence directory: "
			"closedir(%s) failed: %m", path);
	}
	return ret;
}

struct sieve_script_sequence *sieve_file_storage_get_script_sequence
(struct sieve_storage *storage, enum sieve_error *error_r)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	struct sieve_file_script_sequence *fseq = NULL;
	const char *name = storage->script_name;
	const char *file;
	pool_t pool;
	struct stat st;

	/* Specified path can either be a regular file or a directory */
	if ( stat(fstorage->path, &st) != 0 ) {
		switch ( errno ) {
		case ENOENT:
			sieve_storage_set_error(storage,
				SIEVE_ERROR_NOT_FOUND,
				"Script sequence location not found");
			break;
		case EACCES:
			sieve_storage_set_error(storage,
				SIEVE_ERROR_NO_PERMISSION,
				"Script sequence location not accessible");
			sieve_storage_sys_error(storage,
				"Failed to open sieve sequence: "
				"%s",	eacces_error_get("stat", fstorage->path));
			break;
		default:
			sieve_storage_set_critical(storage,
				"Failed to open sieve sequence: "
				"stat(%s) failed: %m", fstorage->path);
			break;
		}
		*error_r = storage->error_code;
		return NULL;
	}

	/* Create sequence object */
	pool = pool_alloconly_create("sieve_file_script_sequence", 1024);
	fseq = p_new(pool, struct sieve_file_script_sequence, 1);
	fseq->pool = pool;
	sieve_script_sequence_init(&fseq->seq, storage);
	i_array_init(&fseq->script_files, 16);

	if ( S_ISDIR(st.st_mode) ) {
		/* Path is directory */
		if (name == 0 || *name == '\0') {
			/* Read all '.sieve' files in directory */
			if (sieve_file_script_sequence_read_dir
				(fseq, fstorage->path) < 0) {
				*error_r = storage->error_code;
				sieve_file_script_sequence_destroy(&fseq->seq);
				return NULL;
			}

		}	else {
			/* Read specific script file */
			file = sieve_script_file_from_name(name);
			file = sieve_file_storage_path_extend(fstorage, file);
			file = p_strdup(pool, file);
			array_append(&fseq->script_files, &file, 1);
		}

	} else {
		/* Path is a file
		   (apparently; we'll see about that once it is opened) */
		file = p_strdup(pool, fstorage->path);
		array_append(&fseq->script_files, &file, 1);
	}
		
	return &fseq->seq;
}

struct sieve_script *sieve_file_script_sequence_next
(struct sieve_script_sequence *seq, enum sieve_error *error_r)
{
	struct sieve_file_script_sequence *fseq =
		(struct sieve_file_script_sequence *)seq;
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)seq->storage;
	struct sieve_file_script *fscript;
	const char *const *files;
	unsigned int count;

	if ( error_r != NULL )
		*error_r = SIEVE_ERROR_NONE;

	files = array_get(&fseq->script_files, &count);

	fscript = NULL;
	while ( fseq->index < count ) {
		fscript = sieve_file_script_open_from_filename
			(fstorage, files[fseq->index++], NULL);
		if (fscript == NULL) {
			if (seq->storage->error_code != SIEVE_ERROR_NOT_FOUND)
				break;
			sieve_storage_clear_error(seq->storage);
		}
	}

	if (fscript == NULL ) {
		if ( error_r != NULL ) 
			*error_r = seq->storage->error_code;
		return NULL;
	}
	return &fscript->script;
}

void sieve_file_script_sequence_destroy(struct sieve_script_sequence *seq)
{
	struct sieve_file_script_sequence *fseq =
		(struct sieve_file_script_sequence *)seq;

	array_free(&fseq->script_files);
	pool_unref(&fseq->pool);
}
