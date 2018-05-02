#ifndef SIEVE_STORAGE_PRIVATE_H
#define SIEVE_STORAGE_PRIVATE_H

#include "sieve.h"
#include "sieve-error-private.h"

#include "sieve-storage.h"

#define MAILBOX_ATTRIBUTE_PREFIX_SIEVE \
	MAILBOX_ATTRIBUTE_PREFIX_DOVECOT_PVT_SERVER"sieve/"
#define MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES \
	MAILBOX_ATTRIBUTE_PREFIX_SIEVE"files/"
#define MAILBOX_ATTRIBUTE_SIEVE_DEFAULT \
	MAILBOX_ATTRIBUTE_PREFIX_SIEVE"default"

#define MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_LINK 'L'
#define MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_SCRIPT 'S'

struct sieve_storage;

ARRAY_DEFINE_TYPE(sieve_storage_class, const struct sieve_storage *);

struct sieve_storage_vfuncs {
	struct sieve_storage *(*alloc)(void);
	void (*destroy)(struct sieve_storage *storage);
	int (*init)
		(struct sieve_storage *storage, const char *const *options,
			enum sieve_error *error_r);

	int (*get_last_change)
		(struct sieve_storage *storage, time_t *last_change_r);
	void (*set_modified)
		(struct sieve_storage *storage, time_t mtime);

	int (*is_singular)(struct sieve_storage *storage);

	/* script access */
	struct sieve_script *(*get_script)
		(struct sieve_storage *storage, const char *name);

	/* script sequence */
	struct sieve_script_sequence *(*get_script_sequence)
		(struct sieve_storage *storage, enum sieve_error *error_r);
	struct sieve_script *(*script_sequence_next)
		(struct sieve_script_sequence *seq, enum sieve_error *error_r);
	void (*script_sequence_destroy)(struct sieve_script_sequence *seq);

	/* active script */
	int (*active_script_get_name)
		(struct sieve_storage *storage, const char **name_r);
	struct sieve_script *(*active_script_open)
		(struct sieve_storage *storage);
	int (*deactivate)
		(struct sieve_storage *storage);
	int (*active_script_get_last_change)
		(struct sieve_storage *storage, time_t *last_change_r);

	/* listing scripts */
	struct sieve_storage_list_context *(*list_init)
		(struct sieve_storage *storage);
	const char *(*list_next)
		(struct sieve_storage_list_context *lctx, bool *active_r);
	int (*list_deinit)
		(struct sieve_storage_list_context *lctx);

	/* saving scripts */
	// FIXME: simplify this API; reduce this mostly to a single save function
	struct sieve_storage_save_context *(*save_init)
		(struct sieve_storage *storage, const char *scriptname,
			struct istream *input);
	int (*save_continue)(struct sieve_storage_save_context *sctx);
	int (*save_finish)(struct sieve_storage_save_context *sctx);
	struct sieve_script *(*save_get_tempscript)
		(struct sieve_storage_save_context *sctx);
	void (*save_cancel)(struct sieve_storage_save_context *sctx);
	int (*save_commit)(struct sieve_storage_save_context *sctx);
	int (*save_as)
		(struct sieve_storage *storage, struct istream *input,
			const char *name);
	int (*save_as_active)
		(struct sieve_storage *storage, struct istream *input,
			time_t mtime);

	/* checking quota */
	int (*quota_havespace)
		(struct sieve_storage *storage, const char *scriptname,
			size_t size, enum sieve_storage_quota *quota_r, uint64_t *limit_r);
};

struct sieve_storage {
	pool_t pool;
	unsigned int refcount;
	struct sieve_instance *svinst;

	const char *driver_name;
	unsigned int version;

	const struct sieve_storage *storage_class;
	struct sieve_storage_vfuncs v;

	uint64_t max_scripts;
	uint64_t max_storage;

	char *error;
	enum sieve_error error_code;

	const char *data;
	const char *location;
	const char *script_name;
	const char *bin_dir;

	const char *default_name;
	const char *default_location;
	struct sieve_storage *default_for;

	struct mail_namespace *sync_inbox_ns;

	enum sieve_storage_flags flags;

	/* this is the main personal storage */
	bool main_storage:1;
	bool allows_synchronization:1;
	bool is_default:1;
};

struct sieve_storage *sieve_storage_alloc
	(struct sieve_instance *svinst, 
		const struct sieve_storage *storage_class, const char *data,
		enum sieve_storage_flags flags, bool main);

int sieve_storage_setup_bindir
	(struct sieve_storage *storage, mode_t mode);

/*
 * Active script
 */

int sieve_storage_active_script_is_default
	(struct sieve_storage *storage);

/*
 * Listing scripts
 */

struct sieve_storage_list_context {
	struct sieve_storage *storage;

	bool seen_active:1; // Just present for assertions
	bool seen_default:1;
};

/*
 * Script sequence
 */

struct sieve_script_sequence {
	struct sieve_storage *storage;
};

/*
 * Saving scripts
 */

struct sieve_storage_save_context {
	pool_t pool;
	struct sieve_storage *storage;

	const char *scriptname, *active_scriptname;
	struct sieve_script *scriptobject;

	struct istream *input;

	time_t mtime;

	bool failed:1;
	bool finished:1;
};

/*
 * Storage class
 */

struct sieve_storage_class_registry;

void sieve_storages_init(struct sieve_instance *svinst);
void sieve_storages_deinit(struct sieve_instance *svinst);

void sieve_storage_class_register
	(struct sieve_instance *svinst,
		const struct sieve_storage *storage_class);
void sieve_storage_class_unregister
	(struct sieve_instance *svinst,
		const struct sieve_storage *storage_class);
const struct sieve_storage *sieve_storage_find_class
	(struct sieve_instance *svinst, const char *name);

/*
 * Built-in storage drivers
 */

/* data (currently only for internal use) */

#define SIEVE_DATA_STORAGE_DRIVER_NAME "data"

extern const struct sieve_storage sieve_data_storage;

/* file */

#define SIEVE_FILE_STORAGE_DRIVER_NAME "file"

extern const struct sieve_storage sieve_file_storage;

struct sieve_storage *sieve_file_storage_init_legacy
	(struct sieve_instance *svinst, const char *active_path,
		const char *storage_path, enum sieve_storage_flags flags,
		enum sieve_error *error_r) ATTR_NULL(6);

/* dict */

#define SIEVE_DICT_STORAGE_DRIVER_NAME "dict"

extern const struct sieve_storage sieve_dict_storage;

/* ldap */

#define SIEVE_LDAP_STORAGE_DRIVER_NAME "ldap"

extern const struct sieve_storage sieve_ldap_storage;

/*
 * Error handling
 */

void sieve_storage_set_internal_error
	(struct sieve_storage *storage);

void sieve_storage_copy_error
	(struct sieve_storage *storage, const struct sieve_storage *source);

void sieve_storage_sys_error
	(struct sieve_storage *storage, const char *fmt, ...)
	ATTR_FORMAT(2, 3);
void sieve_storage_sys_warning
	(struct sieve_storage *storage, const char *fmt, ...)
	ATTR_FORMAT(2, 3);
void sieve_storage_sys_info
	(struct sieve_storage *storage, const char *fmt, ...)
	ATTR_FORMAT(2, 3);
void sieve_storage_sys_debug
	(struct sieve_storage *storage, const char *fmt, ...)
	ATTR_FORMAT(2, 3);

/*
 * Synchronization
 */

int sieve_storage_sync_init
	(struct sieve_storage *storage, struct mail_user *user);
void sieve_storage_sync_deinit
	(struct sieve_storage *storage);

int sieve_storage_sync_script_save
	(struct sieve_storage *storage, const char *name);
int sieve_storage_sync_script_rename
	(struct sieve_storage *storage, const char *oldname,
		const char *newname);
int sieve_storage_sync_script_delete
	(struct sieve_storage *storage, const char *name);

int sieve_storage_sync_script_activate
(struct sieve_storage *storage);
int sieve_storage_sync_deactivate
(struct sieve_storage *storage);


#endif
