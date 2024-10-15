#ifndef SIEVE_FILE_STORAGE_H
#define SIEVE_FILE_STORAGE_H

#include "lib.h"
#include "mail-user.h"

#include "sieve.h"
#include "sieve-script-private.h"
#include "sieve-storage-private.h"

#include "sieve-file-storage-settings.h"

#include <sys/types.h>
#include <sys/stat.h>

#define SIEVE_FILE_READ_BLOCK_SIZE (1024*8)

/* How often to scan tmp/ directory for old files (based on dir's atime) */
#define SIEVE_FILE_STORAGE_TMP_SCAN_SECS (8*60*60)
/* Delete files having ctime older than this from tmp/. 36h is standard. */
#define SIEVE_FILE_STORAGE_TMP_DELETE_SECS (36*60*60)

/*
 * Storage class
 */

struct sieve_file_storage {
	struct sieve_storage storage;

	const char *path;
	const char *active_path;
	const char *active_fname;
	const char *link_path;

	struct stat st;
	struct stat lnk_st;

	mode_t dir_create_mode;
	mode_t file_create_mode;
	gid_t file_create_gid;

	time_t prev_mtime;
};

const char *
sieve_file_storage_path_extend(struct sieve_file_storage *fstorage,
			       const char *filename);

int sieve_file_storage_init_from_path(struct sieve_instance *svinst,
				      const char *path,
				      enum sieve_storage_flags flags,
				      struct sieve_file_storage **fstorage_r,
				      enum sieve_error *error_code_r,
				      const char **error_r);

int sieve_file_storage_pre_modify(struct sieve_storage *storage);

/* Active script */

int sieve_file_storage_active_replace_link(struct sieve_file_storage *fstorage,
					   const char *link_path);
bool sieve_file_storage_active_rescue_regular(
	struct sieve_file_storage *fstorage);

int sieve_file_storage_active_script_get_name(struct sieve_storage *storage,
					      const char **name_r);
int sieve_file_storage_active_script_open(struct sieve_storage *storage,
					  struct sieve_script **script_r);

int sieve_file_storage_active_script_get_file(
	struct sieve_file_storage *fstorage, const char **file_r);
int sieve_file_storage_active_script_is_no_link(
	struct sieve_file_storage *fstorage);

int sieve_file_storage_deactivate(struct sieve_storage *storage);

int sieve_file_storage_active_script_get_last_change(
	struct sieve_storage *storage, time_t *last_change_r);

/* Listing */

int sieve_file_storage_list_init(struct sieve_storage *storage,
				 struct sieve_storage_list_context **lctx_r);
const char *
sieve_file_storage_list_next(struct sieve_storage_list_context *lctx,
			     bool *active);
int sieve_file_storage_list_deinit(struct sieve_storage_list_context *lctx);

/* Saving */

struct sieve_storage_save_context *
sieve_file_storage_save_alloc(struct sieve_storage *storage);
int sieve_file_storage_save_init(struct sieve_storage_save_context *sctx,
				 const char *scriptname, struct istream *input);
int sieve_file_storage_save_continue(struct sieve_storage_save_context *sctx);
int sieve_file_storage_save_finish(struct sieve_storage_save_context *sctx);
struct sieve_script *
sieve_file_storage_save_get_tempscript(struct sieve_storage_save_context *sctx);
int sieve_file_storage_save_commit(struct sieve_storage_save_context *sctx);
void sieve_file_storage_save_cancel(struct sieve_storage_save_context *sctx);

int sieve_file_storage_save_as(struct sieve_storage *storage,
			       struct istream *input, const char *name);
int sieve_file_storage_save_as_active(struct sieve_storage *storage,
				      struct istream *input, time_t mtime);

/* Quota */

int sieve_file_storage_quota_havespace(struct sieve_storage *storage,
				       const char *scriptname, size_t size,
				       enum sieve_storage_quota *quota_r,
				       uint64_t *limit_r);

/*
 * Sieve script filenames
 */

const char *sieve_script_file_get_scriptname(const char *filename);
const char *sieve_script_file_from_name(const char *name);

/*
 * Script class
 */

struct sieve_file_script {
	struct sieve_script script;

	struct stat st;
	struct stat lnk_st;

	const char *path;
	const char *dir_path;
	const char *filename;
	const char *bin_path;
	const char *bin_prefix;

	time_t prev_mtime;
};

int sieve_file_script_init_from_filename(struct sieve_file_storage *fstorage,
					 const char *filename,
					 const char *scriptname,
					 struct sieve_file_script **fscript_r);
int sieve_file_script_open_from_filename(struct sieve_file_storage *fstorage,
					 const char *filename,
					 const char *scriptname,
					 struct sieve_file_script **fscript_r);

int sieve_file_script_init_from_name(struct sieve_file_storage *fstorage,
				     const char *name,
				     struct sieve_file_script **fscript_r);
int sieve_file_script_open_from_name(struct sieve_file_storage *fstorage,
				     const char *name,
				     struct sieve_file_script **fscript_r);

int sieve_file_script_init_from_path(struct sieve_file_storage *fstorage,
				     const char *path, const char *scriptname,
				     struct sieve_file_script **fscript_r);
int sieve_file_script_open_from_path(struct sieve_file_storage *fstorage,
				     const char *path, const char *scriptname,
				     struct sieve_file_script **fscript_r);

/* Return directory where script resides in. Returns NULL if this is not a file
   script.
 */
const char *sieve_file_script_get_dir_path(const struct sieve_script *script);

/* Return full path to file script. Returns NULL if this is not a file script.
 */
const char *sieve_file_script_get_path(const struct sieve_script *script);

/*
 * Script sequence
 */

int sieve_file_script_sequence_init(struct sieve_script_sequence *sseq);
int sieve_file_script_sequence_next(struct sieve_script_sequence *sseq,
				    struct sieve_script **script_r);
void sieve_file_script_sequence_destroy(struct sieve_script_sequence *sseq);

#endif
