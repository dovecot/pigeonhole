#ifndef SIEVE_SCRIPT_H
#define SIEVE_SCRIPT_H

#include "sieve-common.h"

#include <sys/types.h>


/*
 * Sieve script name
 */

bool sieve_script_name_is_valid(const char *scriptname);

/*
 * Sieve script file
 */

bool sieve_script_file_has_extension(const char *filename);

/*
 * Sieve script class
 */

void sieve_script_class_register(struct sieve_instance *svinst,
				 const struct sieve_script *script_class);
void sieve_script_class_unregister(struct sieve_instance *svinst,
				   const struct sieve_script *script_class);

/*
 * Sieve script instance
 */

struct sieve_script;

ARRAY_DEFINE_TYPE(sieve_script, struct sieve_script *);

int sieve_script_create(struct sieve_instance *svinst,
			const char *location, const char *name,
			struct sieve_script **script_r,
			enum sieve_error *error_code_r);

void sieve_script_ref(struct sieve_script *script);
void sieve_script_unref(struct sieve_script **script);

int sieve_script_open(struct sieve_script *script,
		      enum sieve_error *error_code_r);
int sieve_script_open_as(struct sieve_script *script, const char *name,
			 enum sieve_error *error_code_r);

int sieve_script_create_open(struct sieve_instance *svinst,
			     const char *location, const char *name,
			     struct sieve_script **script_r,
			     enum sieve_error *error_code_r);
int sieve_script_check(struct sieve_instance *svinst, const char *location,
		       const char *name, enum sieve_error *error_code_r);

/*
 * Data script
 */

struct sieve_script *
sieve_data_script_create_from_input(struct sieve_instance *svinst,
				    const char *name, struct istream *input);

/*
 * Binary
 */

int sieve_script_binary_read_metadata(struct sieve_script *script,
				      struct sieve_binary_block *sblock,
				      sieve_size_t *offset);
void sieve_script_binary_write_metadata(struct sieve_script *script,
					struct sieve_binary_block *sblock);
bool sieve_script_binary_dump_metadata(struct sieve_script *script,
				       struct sieve_dumptime_env *denv,
				       struct sieve_binary_block *sblock,
				       sieve_size_t *offset) ATTR_NULL(1);

int sieve_script_binary_load(struct sieve_script *script,
			     struct sieve_binary **sbin_r,
			     enum sieve_error *error_code_r);
int sieve_script_binary_save(struct sieve_script *script,
			     struct sieve_binary *sbin, bool update,
			     enum sieve_error *error_code_r);

const char *sieve_script_binary_get_prefix(struct sieve_script *script);

/*
 * Stream management
 */

int sieve_script_get_stream(struct sieve_script *script,
			    struct istream **stream_r,
			    enum sieve_error *error_code_r);

/*
 * Management
 */

// FIXME: check read/write flag!

int sieve_script_rename(struct sieve_script *script, const char *newname);
int sieve_script_is_active(struct sieve_script *script);
int sieve_script_activate(struct sieve_script *script, time_t mtime);
int sieve_script_delete(struct sieve_script *script, bool ignore_active);

/*
 * Properties
 */

const char *sieve_script_name(const struct sieve_script *script) ATTR_PURE;
const char *sieve_script_label(const struct sieve_script *script) ATTR_PURE;
const char *sieve_script_location(const struct sieve_script *script) ATTR_PURE;
struct sieve_instance *
sieve_script_svinst(const struct sieve_script *script) ATTR_PURE;

int sieve_script_get_size(struct sieve_script *script, uoff_t *size_r);
bool sieve_script_is_open(const struct sieve_script *script) ATTR_PURE;
bool sieve_script_is_default(const struct sieve_script *script) ATTR_PURE;

const char *
sieve_file_script_get_dir_path(const struct sieve_script *script) ATTR_PURE;
const char *
sieve_file_script_get_path(const struct sieve_script *script) ATTR_PURE;

/*
 * Comparison
 */

int sieve_script_cmp(const struct sieve_script *script1,
		     const struct sieve_script *script2);
static inline bool
sieve_script_equals(const struct sieve_script *script1,
		    const struct sieve_script *script2)
{
	return (sieve_script_cmp(script1, script2) == 0);
}

unsigned int sieve_script_hash(const struct sieve_script *script);

/*
 * Error handling
 */

const char *sieve_script_get_last_error(struct sieve_script *script,
					enum sieve_error *error_code_r);
const char *sieve_script_get_last_error_lcase(struct sieve_script *script);

/*
 * Script sequence
 */

struct sieve_script_sequence;

int sieve_script_sequence_create(struct sieve_instance *svinst,
				 const char *location,
				 struct sieve_script_sequence **sseq_r,
				 enum sieve_error *error_code_r);
int sieve_script_sequence_next(struct sieve_script_sequence *sseq,
			       struct sieve_script **script_r,
			       enum sieve_error *error_code_r);
void sieve_script_sequence_free(struct sieve_script_sequence **_seq);

#endif
