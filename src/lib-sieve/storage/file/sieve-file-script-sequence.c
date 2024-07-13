/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "array.h"
#include "eacces-error.h"

#include "sieve-common.h"
#include "sieve-script-private.h"

#include "sieve-file-storage.h"

#include <stdio.h>
#include <dirent.h>

/*
 * Script sequence
 */

struct sieve_file_script_sequence {
	pool_t pool;

	ARRAY_TYPE(const_string) script_files;
	unsigned int index;

	bool storage_is_file:1;
};

static int
sieve_file_script_sequence_read_dir(struct sieve_script_sequence *sseq,
				    struct sieve_file_script_sequence *fseq,
				    const char *path)
{
	struct sieve_storage *storage = sseq->storage;
	DIR *dirp;
	int ret = 0;

	/* Open the directory */
	dirp = opendir(path);
	if (dirp == NULL) {
		switch (errno) {
		case ENOENT:
			sieve_storage_set_error(
				storage, SIEVE_ERROR_NOT_FOUND,
				"Script sequence location not found");
			break;
		case EACCES:
			sieve_storage_set_error(
				storage, SIEVE_ERROR_NO_PERMISSION,
				"Script sequence location not accessible");
			e_error(storage->event,
				"Failed to open sieve sequence: %s",
				eacces_error_get("stat", path));
			break;
		default:
			sieve_storage_set_critical(
				storage, "Failed to open sieve sequence: "
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
		dp = readdir(dirp);
		if (dp == NULL)
			break;

		if (!sieve_script_file_has_extension(dp->d_name))
			continue;

		file = NULL;
		T_BEGIN {
			if (path[strlen(path)-1] == '/')
				file = t_strconcat(path, dp->d_name, NULL);
			else
				file = t_strconcat(path, "/", dp->d_name, NULL);

			if (stat(file, &st) == 0 && S_ISREG(st.st_mode))
				file = p_strdup(fseq->pool, dp->d_name);
			else
				file = NULL;
		} T_END;

		if (file == NULL)
			continue;

		/* Insert into sorted array */
		files = array_get(&fseq->script_files, &count);
		for (i = 0; i < count; i++) {
			if (strcmp(file, files[i]) < 0)
				break;
		}

		if (i == count)
			array_append(&fseq->script_files, &file, 1);
		else
			array_insert(&fseq->script_files, i, &file, 1);
	}

	if (errno != 0) {
		sieve_storage_set_critical(
			storage, "Failed to read sequence directory: "
			"readdir(%s) failed: %m", path);
		ret = -1;
	}

	/* Close the directory */
	if (dirp != NULL && closedir(dirp) < 0) {
		e_error(storage->event,
			"Failed to close sequence directory: "
			"closedir(%s) failed: %m", path);
	}
	return ret;
}

int sieve_file_script_sequence_init(struct sieve_script_sequence *sseq,
				    enum sieve_error *error_code_r)
{
	struct sieve_storage *storage = sseq->storage;
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	struct sieve_file_script_sequence *fseq = NULL;
	const char *name = storage->script_name;
	const char *file;
	pool_t pool;
	struct stat st;

	/* Specified path can either be a regular file or a directory */
	if (stat(fstorage->path, &st) != 0) {
		switch (errno) {
		case ENOENT:
			sieve_storage_set_error(
				storage, SIEVE_ERROR_NOT_FOUND,
				"Script sequence location not found");
			break;
		case EACCES:
			sieve_storage_set_error(
				storage, SIEVE_ERROR_NO_PERMISSION,
				"Script sequence location not accessible");
			e_error(storage->event,
				"Failed to open sieve sequence: %s",
				eacces_error_get("stat", fstorage->path));
			break;
		default:
			sieve_storage_set_critical(
				storage, "Failed to open sieve sequence: "
				"stat(%s) failed: %m", fstorage->path);
			break;
		}
		*error_code_r = storage->error_code;
		return -1;
	}

	/* Create sequence object */
	pool = pool_alloconly_create("sieve_file_script_sequence", 1024);
	fseq = p_new(pool, struct sieve_file_script_sequence, 1);
	fseq->pool = pool;
	sseq->storage_data = fseq;

	if (S_ISDIR(st.st_mode)) {
		i_array_init(&fseq->script_files, 16);

		/* Path is directory */
		if (name == 0 || *name == '\0') {
			/* Read all '.sieve' files in directory */
			if (sieve_file_script_sequence_read_dir(
				sseq, fseq, fstorage->path) < 0) {
				*error_code_r = storage->error_code;
				pool_unref(&fseq->pool);
				return -1;
			}

		}	else {
			/* Read specific script file */
			file = sieve_script_file_from_name(name);
			file = p_strdup(pool, file);
			array_append(&fseq->script_files, &file, 1);
		}

	} else {
		/* Path is a file
		   (apparently; we'll see about that once it is opened) */
		fseq->storage_is_file = TRUE;
	}

	return 0;
}

int sieve_file_script_sequence_next(struct sieve_script_sequence *sseq,
				    struct sieve_script **script_r,
				    enum sieve_error *error_code_r)
{
	struct sieve_file_script_sequence *fseq = sseq->storage_data;
	struct sieve_storage *storage = sseq->storage;
	struct sieve_file_storage *fstorage =
		container_of(storage, struct sieve_file_storage, storage);
	struct sieve_file_script *fscript = NULL;
	const char *const *files;
	unsigned int count;
	int ret;

	if (fseq->storage_is_file) {
		i_assert(fseq->index <= 1);
		if (fseq->index++ > 0)
			return 0;

		ret = sieve_file_script_open_from_name(fstorage, NULL,
						       &fscript);
	} else {
		files = array_get(&fseq->script_files, &count);

		i_assert(fseq->index <= count);
		if (fseq->index == count)
			return 0;

		for (;;) {
			ret = sieve_file_script_open_from_filename(
				fstorage, files[fseq->index++], NULL,
				&fscript);
			if (ret == 0)
				break;
			if (storage->error_code != SIEVE_ERROR_NOT_FOUND)
				break;
			sieve_storage_clear_error(storage);
			if (fseq->index >= count)
				return 0;
		}
	}

	if (ret < 0) {
		if (storage->error_code == SIEVE_ERROR_NOT_FOUND)
			return 0;
		*error_code_r = storage->error_code;
		return -1;
	}
	*script_r = &fscript->script;
	return 1;
}

void sieve_file_script_sequence_destroy(struct sieve_script_sequence *sseq)
{
	struct sieve_file_script_sequence *fseq = sseq->storage_data;

	if (array_is_created(&fseq->script_files))
		array_free(&fseq->script_files);
	pool_unref(&fseq->pool);
}
