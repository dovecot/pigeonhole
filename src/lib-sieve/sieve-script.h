/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_SCRIPT_H
#define __SIEVE_SCRIPT_H

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

void sieve_script_class_register
	(struct sieve_instance *svinst, const struct sieve_script *script_class);
void sieve_script_class_unregister
	(struct sieve_instance *svinst, const struct sieve_script *script_class);	

/*
 * Sieve script instance
 */

struct sieve_script;

ARRAY_DEFINE_TYPE(sieve_script, struct sieve_script *);

struct sieve_script *sieve_script_create
	(struct sieve_instance *svinst, const char *location, const char *name,
		enum sieve_error *error_r)
		ATTR_NULL(3,4);

void sieve_script_ref(struct sieve_script *script);
void sieve_script_unref(struct sieve_script **script);

int sieve_script_open
	(struct sieve_script *script, enum sieve_error *error_r)
		ATTR_NULL(2);
int sieve_script_open_as
	(struct sieve_script *script, const char *name,
		enum sieve_error *error_r) ATTR_NULL(3);

struct sieve_script *sieve_script_create_open
	(struct sieve_instance *svinst, const char *location,
		const char *name, enum sieve_error *error_r)
		ATTR_NULL(3, 4);
int sieve_script_check
	(struct sieve_instance *svinst, const char *location,
		const char *name, enum sieve_error *error_r)
		ATTR_NULL(3, 4);

/*
 * Data script
 */

struct sieve_script *sieve_data_script_create_from_input
	(struct sieve_instance *svinst, const char *name,
		struct istream *input);

/*
 * Binary
 */

int sieve_script_binary_read_metadata
	(struct sieve_script *script, struct sieve_binary_block *sblock,
		sieve_size_t *offset);
void sieve_script_binary_write_metadata
	(struct sieve_script *script, struct sieve_binary_block *sblock);
bool sieve_script_binary_dump_metadata
	(struct sieve_script *script, struct sieve_dumptime_env *denv,
		struct sieve_binary_block *sblock, sieve_size_t *offset)
		ATTR_NULL(1);

struct sieve_binary *sieve_script_binary_load
	(struct sieve_script *script, enum sieve_error *error_r);
int sieve_script_binary_save
	(struct sieve_script *script, struct sieve_binary *sbin, bool update,
		enum sieve_error *error_r) ATTR_NULL(4);

const char *sieve_script_binary_get_prefix
	(struct sieve_script *script);

/*
 * Stream management
 */

int sieve_script_get_stream
	(struct sieve_script *script, struct istream **stream_r,
		enum sieve_error *error_r) ATTR_NULL(3);

/*
 * Management
 */

// FIXME: check read/write flag!

int sieve_script_rename
	(struct sieve_script *script, const char *newname);
int sieve_script_is_active(struct sieve_script *script);
int sieve_script_activate
	(struct sieve_script *script, time_t mtime);
int sieve_script_delete
	(struct sieve_script *script, bool ignore_active);

/*
 * Properties
 */

const char *sieve_script_name
	(const struct sieve_script *script) ATTR_PURE;
const char *sieve_script_location
	(const struct sieve_script *script) ATTR_PURE;
struct sieve_instance *sieve_script_svinst
	(const struct sieve_script *script) ATTR_PURE;

int sieve_script_get_size(struct sieve_script *script, uoff_t *size_r);
bool sieve_script_is_open
	(const struct sieve_script *script) ATTR_PURE;
bool sieve_script_is_default
	(const struct sieve_script *script) ATTR_PURE;

const char *sieve_file_script_get_dirpath
	(const struct sieve_script *script) ATTR_PURE;
const char *sieve_file_script_get_path
	(const struct sieve_script *script) ATTR_PURE;

/*
 * Comparison
 */

bool sieve_script_equals
	(const struct sieve_script *script, const struct sieve_script *other);

unsigned int sieve_script_hash(const struct sieve_script *script);
static inline int sieve_script_cmp
(const struct sieve_script *script, const struct sieve_script *other)
{
	return ( sieve_script_equals(script, other) ? 0 : -1 );
}

/*
 * Error handling
 */

const char *sieve_script_get_last_error
	(struct sieve_script *script, enum sieve_error *error_r)
	ATTR_NULL(2);
const char *sieve_script_get_last_error_lcase
	(struct sieve_script *script);

/*
 * Script sequence
 */

struct sieve_script_sequence;

struct sieve_script_sequence *sieve_script_sequence_create
(struct sieve_instance *svinst, const char *location,
	enum sieve_error *error_r);
struct sieve_script *sieve_script_sequence_next
	(struct sieve_script_sequence *seq, enum sieve_error *error_r);
void sieve_script_sequence_free(struct sieve_script_sequence **_seq);

#endif /* __SIEVE_SCRIPT_H */
