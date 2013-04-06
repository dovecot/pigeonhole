/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_SCRIPT_PRIVATE_H
#define __SIEVE_SCRIPT_PRIVATE_H

#include "sieve-script.h"

/*
 * Script object
 */

struct sieve_script_vfuncs {
	struct sieve_script *(*alloc)(void);
	void (*destroy)(struct sieve_script *script);

	int (*open)
		(struct sieve_script *script, const char *data,
			const char *const *options, enum sieve_error *error_r);

	int (*get_stream)
		(struct sieve_script *script, struct istream **stream_r,
			enum sieve_error *error_r);
	
	int (*binary_read_metadata)
		(struct sieve_script *_script, struct sieve_binary_block *sblock,
			sieve_size_t *offset);
	void (*binary_write_metadata)
		(struct sieve_script *script, struct sieve_binary_block *sblock);
	struct sieve_binary *(*binary_load)
		(struct sieve_script *script, enum sieve_error *error_r);
	int (*binary_save)
		(struct sieve_script *script, struct sieve_binary *sbin,
			bool update, enum sieve_error *error_r);

	int (*get_size)
		(const struct sieve_script *script, uoff_t *size_r);

	bool (*equals)
		(const struct sieve_script *script, const struct sieve_script *other);
};

struct sieve_script {
	pool_t pool;
	unsigned int refcount;

	const char *driver_name;
	const struct sieve_script *script_class;
	struct sieve_script_vfuncs v;

	struct sieve_instance *svinst;
	struct sieve_error_handler *ehandler;

	const char *name;
	const char *data;
	const char *location;
	const char *bin_dir;

	/* Stream */
	struct istream *stream;

	unsigned int open:1;
};

void sieve_script_init
	(struct sieve_script *script, struct sieve_instance *svinst,
		const struct sieve_script *script_class, const char *data,
		const char *name, struct sieve_error_handler *ehandler);

int sieve_script_setup_bindir
	(struct sieve_script *script, mode_t mode);

/*
 * Built-in file script driver
 */

#define SIEVE_FILE_SCRIPT_DRIVER_NAME "file"

struct sieve_file_script {
	struct sieve_script script;

	struct stat st;
	struct stat lnk_st;

	const char *path;
	const char *dirpath;
	const char *filename;
	const char *binpath;

	int fd;
};

extern const struct sieve_script sieve_file_script;

/*
 * Built-in dict script driver
 */

#define SIEVE_DICT_SCRIPT_DRIVER_NAME "dict"

extern const struct sieve_script sieve_dict_script;

#endif /* __SIEVE_SCRIPT_PRIVATE_H */
