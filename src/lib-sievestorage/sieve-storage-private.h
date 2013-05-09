/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_STORAGE_PRIVATE_H
#define __SIEVE_STORAGE_PRIVATE_H

#include "sieve.h"
#include "sieve-error-private.h"

#include "sieve-storage.h"

#define SIEVE_READ_BLOCK_SIZE (1024*8)

/* How often to scan tmp/ directory for old files (based on dir's atime) */
#define SIEVE_STORAGE_TMP_SCAN_SECS (8*60*60)
/* Delete files having ctime older than this from tmp/. 36h is standard. */
#define SIEVE_STORAGE_TMP_DELETE_SECS (36*60*60)

struct sieve_storage;

struct sieve_storage_ehandler {
	struct sieve_error_handler handler;
	struct sieve_storage *storage;
};

/* All methods returning int return either TRUE or FALSE. */
struct sieve_storage {
	pool_t pool;
	struct sieve_instance *svinst;

	char *name;
	char *dir;

	/* Private */
	char *active_path;
	char *active_fname;
	char *link_path;
	char *error;
	char *username; /* name of user accessing the storage */

	mode_t dir_create_mode;
	mode_t file_create_mode;
	gid_t file_create_gid;

	struct mailbox *inbox;

	uint64_t max_scripts;
	uint64_t max_storage;

	enum sieve_error error_code;
	struct sieve_error_handler *ehandler;

	enum sieve_storage_flags flags;
	time_t prev_mtime;
};

struct sieve_script *sieve_storage_script_init_from_path
	(struct sieve_storage *storage, const char *path, const char *scriptname);

void sieve_storage_inbox_script_attribute_set
	(struct sieve_storage *storage, const char *name);
void sieve_storage_inbox_script_attribute_rename
	(struct sieve_storage *storage, const char *oldname, const char *newname);
void sieve_storage_inbox_script_attribute_unset
	(struct sieve_storage *storage, const char *name);


#endif

