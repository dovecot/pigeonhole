/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
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
 * Sieve script object
 */

struct sieve_script;

ARRAY_DEFINE_TYPE(sieve_scripts, struct sieve_script *);

struct sieve_script *sieve_script_create
	(struct sieve_instance *svinst, const char *location, const char *name,
		struct sieve_error_handler *ehandler, enum sieve_error *error_r);

void sieve_script_ref(struct sieve_script *script);
void sieve_script_unref(struct sieve_script **script);

int sieve_script_open
	(struct sieve_script *script, enum sieve_error *error_r);
int sieve_script_open_as
	(struct sieve_script *script, const char *name, enum sieve_error *error_r);

struct sieve_script *sieve_script_create_open
	(struct sieve_instance *svinst, const char *location, const char *name,
		struct sieve_error_handler *ehandler, enum sieve_error *error_r);
struct sieve_script *sieve_script_create_open_as
	(struct sieve_instance *svinst, const char *location, const char *name,
		struct sieve_error_handler *ehandler, enum sieve_error *error_r);

/*
 * Accessors
 */

const char *sieve_script_name(const struct sieve_script *script);
const char *sieve_script_location(const struct sieve_script *script);
struct sieve_instance *sieve_script_svinst(const struct sieve_script *script);

bool sieve_script_is_open(const struct sieve_script *script);

/*
 * Saving/loading Sieve binaries
 */

int sieve_script_binary_read_metadata
	(struct sieve_script *script, struct sieve_binary_block *sblock,
		sieve_size_t *offset);
void sieve_script_binary_write_metadata
	(struct sieve_script *script, struct sieve_binary_block *sblock);

struct sieve_binary *sieve_script_binary_load
	(struct sieve_script *script, enum sieve_error *error_r);
int sieve_script_binary_save
	(struct sieve_script *script, struct sieve_binary *sbin, bool update,
		enum sieve_error *error_r);

/*
 * Stream management
 */

int sieve_script_get_stream
	(struct sieve_script *script, struct istream **stream_r,
		enum sieve_error *error_r);
int sieve_script_get_size(struct sieve_script *script, uoff_t *size_r);

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


#endif /* __SIEVE_SCRIPT_H */
