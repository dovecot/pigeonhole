#ifndef SIEVE_SCRIPT_PRIVATE_H
#define SIEVE_SCRIPT_PRIVATE_H

#include "sieve-common.h"
#include "sieve-script.h"

/*
 * Script object
 */

struct sieve_script_vfuncs {
	void (*destroy)(struct sieve_script *script);

	int (*open)(struct sieve_script *script);
	int (*get_stream)(struct sieve_script *script,
			  struct istream **stream_r);

	/* binary */
	int (*binary_read_metadata)(struct sieve_script *_script,
				    struct sieve_binary_block *sblock,
				    sieve_size_t *offset);
	void (*binary_write_metadata)(struct sieve_script *script,
				      struct sieve_binary_block *sblock);
	bool (*binary_dump_metadata)(struct sieve_script *script,
				     struct sieve_dumptime_env *denv,
				     struct sieve_binary_block *sblock,
				     sieve_size_t *offset);
	int (*binary_load)(struct sieve_script *script,
			   struct sieve_binary **sbin_r);
	int (*binary_save)(struct sieve_script *script,
			   struct sieve_binary *sbin, bool update);
	const char *(*binary_get_prefix)(struct sieve_script *script);

	/* management */
	int (*rename)(struct sieve_script *script, const char *newname);
	int (*delete)(struct sieve_script *script);
	int (*is_active)(struct sieve_script *script);
	int (*activate)(struct sieve_script *script);

	/* properties */
	int (*get_size)(const struct sieve_script *script, uoff_t *size_r);

	/* matching */
	int (*cmp)(const struct sieve_script *script1,
		   const struct sieve_script *script2);
};

struct sieve_script {
	pool_t pool;
	unsigned int refcount;
	struct sieve_storage *storage;
	struct event *event;

	const char *driver_name;
	const struct sieve_script *script_class;
	struct sieve_script_vfuncs v;

	const char *name;

	/* Stream */
	struct istream *stream;

	bool open:1;
};

void sieve_script_init(struct sieve_script *script,
		       struct sieve_storage *storage,
		       const struct sieve_script *script_class,
		       const char *name);

/*
 * Binary
 */

int sieve_script_binary_load_default(struct sieve_script *script,
				     const char *path,
				     struct sieve_binary **sbin_r);
int sieve_script_binary_save_default(struct sieve_script *script,
				     struct sieve_binary *sbin,
				     const char *path, bool update,
				     mode_t save_mode);

/*
 * Built-in script drivers
 */

extern const struct sieve_script sieve_data_script;
extern const struct sieve_script sieve_file_script;
extern const struct sieve_script sieve_dict_script;
extern const struct sieve_script sieve_ldap_script;

/*
 * Error handling
 */

void sieve_script_set_error(struct sieve_script *script,
			    enum sieve_error error_code,
			    const char *fmt, ...) ATTR_FORMAT(3, 4);
void sieve_script_set_internal_error(struct sieve_script *script);
void sieve_script_set_critical(struct sieve_script *script,
			       const char *fmt, ...) ATTR_FORMAT(2, 3);
void sieve_script_set_not_found_error(struct sieve_script *script,
				      const char *name);

/*
 * Script sequence
 */

struct sieve_script_sequence {
	struct sieve_storage_sequence *storage_seq;
	struct sieve_storage *storage;
	void *storage_data;
};

#endif
