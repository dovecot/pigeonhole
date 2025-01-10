#ifndef SIEVE_DICT_STORAGE_H
#define SIEVE_DICT_STORAGE_H

#include "sieve.h"
#include "sieve-script-private.h"
#include "sieve-storage-private.h"

#define DICT_SIEVE_PATH DICT_PATH_PRIVATE"sieve/"
#define DICT_SIEVE_NAME_PATH DICT_SIEVE_PATH"name/"
#define DICT_SIEVE_DATA_PATH DICT_SIEVE_PATH"data/"

#define SIEVE_DICT_SCRIPT_DEFAULT "default"

/*
 * Storage class
 */

struct sieve_dict_storage {
	struct sieve_storage storage;

	const char *username;
	const char *uri;

	struct dict *dict;
};

int sieve_dict_storage_get_dict(struct sieve_dict_storage *dstorage,
				struct dict **dict_r,
				enum sieve_error *error_code_r);

int sieve_dict_storage_active_script_get_name(struct sieve_storage *storage,
					      const char **name_r);

/*
 * Script class
 */

struct sieve_dict_script {
	struct sieve_script script;

	struct dict *dict;

	pool_t data_pool;
	const char *data_id;
	const char *data;

	const char *binpath;
};

struct sieve_dict_script *
sieve_dict_script_init(struct sieve_dict_storage *dstorage, const char *name);

/*
 * Script sequence
 */

struct sieve_script_sequence *
sieve_dict_storage_get_script_sequence(struct sieve_storage *storage,
				       enum sieve_error *error_code_r);

int sieve_dict_script_sequence_next(struct sieve_script_sequence *sseq,
				    struct sieve_script **script_r,
				    enum sieve_error *error_code_r);
void sieve_dict_script_sequence_destroy(struct sieve_script_sequence *sseq);

#endif
