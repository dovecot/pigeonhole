#ifndef SIEVE_STORAGE_H
#define SIEVE_STORAGE_H

#include "sieve-common.h"

#define MAILBOX_ATTRIBUTE_PREFIX_SIEVE \
	MAILBOX_ATTRIBUTE_PREFIX_DOVECOT_PVT_SERVER"sieve/"
#define MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES \
	MAILBOX_ATTRIBUTE_PREFIX_SIEVE"files/"
#define MAILBOX_ATTRIBUTE_SIEVE_DEFAULT \
	MAILBOX_ATTRIBUTE_PREFIX_SIEVE"default"

#define MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_LINK 'L'
#define MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_SCRIPT 'S'

/*
 * Storage name
 */

bool sieve_storage_name_is_valid(const char *name);

/*
 * Storage type
 */

/* Common storage types; others may be defined in specific contexts. */
#define SIEVE_STORAGE_TYPE_ANY "any"
#define SIEVE_STORAGE_TYPE_PERSONAL "personal"
#define SIEVE_STORAGE_TYPE_DEFAULT "default"
#define SIEVE_STORAGE_TYPE_GLOBAL "global"
#define SIEVE_STORAGE_TYPE_BEFORE "before"
#define SIEVE_STORAGE_TYPE_AFTER "after"
#define SIEVE_STORAGE_TYPE_DISCARD "discard"

/*
 * Storage object
 */

enum sieve_storage_flags {
	/* Storage is opened for read/write access (e.g. ManageSieve) */
	SIEVE_STORAGE_FLAG_READWRITE         = 0x01,
	/* This storage is used for synchronization (and not normal ManageSieve)
	 */
	SIEVE_STORAGE_FLAG_SYNCHRONIZING     = 0x02
};

struct sieve_storage;

/* Determine whether storage driver exists. */
bool sieve_storage_class_exists(struct sieve_instance *svinst,
				const char *name);

int sieve_storage_create(struct sieve_instance *svinst,
			 struct event *event, const char *cause,
			 const char *storage_name,
			 enum sieve_storage_flags flags,
			 struct sieve_storage **storage_r,
			 enum sieve_error *error_code_r, const char **error_r);
int sieve_storage_create_auto(struct sieve_instance *svinst,
			      struct event *event,
			      const char *cause, const char *type,
			      enum sieve_storage_flags flags,
			      struct sieve_storage **storage_r,
			      enum sieve_error *error_code_r,
			      const char **error_r);
int sieve_storage_create_personal(struct sieve_instance *svinst,
				  struct mail_user *user, const char *cause,
				  enum sieve_storage_flags flags,
				  struct sieve_storage **storage_r,
				  enum sieve_error *error_code_r);

void sieve_storage_ref(struct sieve_storage *storage);
void sieve_storage_unref(struct sieve_storage **_storage);

/*
 * Script access
 */

int sieve_storage_get_script_direct(struct sieve_storage *storage,
				    const char *name,
				    struct sieve_script **script_r,
				    enum sieve_error *error_code_r);
int sieve_storage_get_script(struct sieve_storage *storage, const char *name,
			     struct sieve_script **script_r,
			     enum sieve_error *error_code_r);

int sieve_storage_open_script(struct sieve_storage *storage, const char *name,
			      struct sieve_script **script_r,
			      enum sieve_error *error_code_r);
int sieve_storage_check_script(struct sieve_storage *storage, const char *name,
			       enum sieve_error *error_code_r);

/*
 * Active script
 */

int sieve_storage_active_script_get_name(struct sieve_storage *storage,
					 const char **name_r);

int sieve_storage_active_script_open(struct sieve_storage *storage,
				     struct sieve_script **script_r,
				     enum sieve_error *error_code_r);

int sieve_storage_active_script_get_last_change(struct sieve_storage *storage,
						time_t *last_change_r);

/*
 * Listing scripts in storage
 */

struct sieve_storage_list_context;

/* Create a context for listing the scripts in the storage */
int sieve_storage_list_init(struct sieve_storage *storage,
			    struct sieve_storage_list_context **lctx_r);
/* Get the next script in the storage. */
const char *sieve_storage_list_next(struct sieve_storage_list_context *lctx,
				    bool *active_r) ATTR_NULL(2);
/* Destroy the listing context */
int sieve_storage_list_deinit(struct sieve_storage_list_context **lctx);

/*
 * Saving scripts in storage
 */

struct sieve_storage_save_context;

struct sieve_storage_save_context *
sieve_storage_save_init(struct sieve_storage *storage, const char *scriptname,
			struct istream *input);

int sieve_storage_save_continue(struct sieve_storage_save_context *sctx);

int sieve_storage_save_finish(struct sieve_storage_save_context *sctx);

struct sieve_script *
sieve_storage_save_get_tempscript(struct sieve_storage_save_context *sctx);

bool sieve_storage_save_will_activate(struct sieve_storage_save_context *sctx);

void sieve_storage_save_set_mtime(struct sieve_storage_save_context *sctx,
				  time_t mtime);

void sieve_storage_save_cancel(struct sieve_storage_save_context **sctx);

int sieve_storage_save_commit(struct sieve_storage_save_context **sctx);

int sieve_storage_save_as(struct sieve_storage *storage, struct istream *input,
			  const char *name);

/* Saves input directly as a regular file at the active script path.
 * This is needed for the doveadm-sieve plugin.
 */
int sieve_storage_save_as_active(struct sieve_storage *storage,
				 struct istream *input, time_t mtime);

/*
 * Management
 */

int sieve_storage_deactivate(struct sieve_storage *storage, time_t mtime);

/*
 * Storage quota
 */

enum sieve_storage_quota {
	SIEVE_STORAGE_QUOTA_NONE,
	SIEVE_STORAGE_QUOTA_MAXSIZE,
	SIEVE_STORAGE_QUOTA_MAXSCRIPTS,
	SIEVE_STORAGE_QUOTA_MAXSTORAGE
};

bool sieve_storage_quota_validsize(struct sieve_storage *storage, size_t size,
				   uint64_t *limit_r);

uint64_t sieve_storage_quota_max_script_size(struct sieve_storage *storage);

int sieve_storage_quota_havespace(struct sieve_storage *storage,
				  const char *scriptname, size_t size,
				  enum sieve_storage_quota *quota_r,
				  uint64_t *limit_r);

/*
 * Properties
 */

const char *sieve_storage_name(const struct sieve_storage *storage) ATTR_PURE;
const char *sieve_storage_location(const struct sieve_storage *storage)
	ATTR_PURE;
bool sieve_storage_is_default(const struct sieve_storage *storage) ATTR_PURE;
bool sieve_storage_is_personal(struct sieve_storage *storage) ATTR_PURE;

int sieve_storage_is_singular(struct sieve_storage *storage);

/*
 * Error handling
 */

void sieve_storage_clear_error(struct sieve_storage *storage);

void sieve_storage_set_error(struct sieve_storage *storage,
			     enum sieve_error error_code, const char *fmt, ...)
			     ATTR_FORMAT(3, 4);
void sieve_storage_set_critical(struct sieve_storage *storage,
				const char *fmt, ...) ATTR_FORMAT(2, 3);

const char *sieve_storage_get_last_error(struct sieve_storage *storage,
					 enum sieve_error *error_code_r);

/*
 *
 */

int sieve_storage_get_last_change(struct sieve_storage *storage,
				  time_t *last_change_r);
void sieve_storage_set_modified(struct sieve_storage *storage,
				time_t mtime);

/*
 * Storage sequence
 */

struct sieve_storage_sequence;

int sieve_storage_sequence_create(struct sieve_instance *svinst,
				  struct event *event_parent,
				  const char *cause, const char *type,
				  struct sieve_storage_sequence **sseq_r,
				  enum sieve_error *error_code_r,
				  const char **error_r);
int sieve_storage_sequence_next(struct sieve_storage_sequence *sseq,
				struct sieve_storage **storage_r,
				enum sieve_error *error_code_r,
				const char **error_r);
void sieve_storage_sequence_free(struct sieve_storage_sequence **_sseq);

#endif
