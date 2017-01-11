/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "dict.h"

#include "sieve-common.h"
#include "sieve-error.h"

#include "sieve-dict-storage.h"

/*
 * Storage class
 */

static struct sieve_storage *sieve_dict_storage_alloc(void)
{
	struct sieve_dict_storage *dstorage;
	pool_t pool;

	pool = pool_alloconly_create("sieve_dict_storage", 1024);
	dstorage = p_new(pool, struct sieve_dict_storage, 1);
	dstorage->storage = sieve_dict_storage;
	dstorage->storage.pool = pool;

	return &dstorage->storage;
}

static int sieve_dict_storage_init
(struct sieve_storage *storage, const char *const *options,
	enum sieve_error *error_r)
{
	struct sieve_dict_storage *dstorage =
		(struct sieve_dict_storage *)storage;
	struct sieve_instance *svinst = storage->svinst;
	const char *uri = storage->location, *username = NULL;

	if ( options != NULL ) {
		while ( *options != NULL ) {
			const char *option = *options;

			if ( strncasecmp(option, "user=", 5) == 0 && option[5] != '\0' ) {
				username = option+5;
			} else {
				sieve_storage_set_critical(storage,
					"Invalid option `%s'", option);
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
				return -1;
			}

			options++;
		}
	}

	if ( username == NULL ) {
		if ( svinst->username == NULL ) {
			sieve_storage_set_critical(storage,
				"No username specified");
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			return -1;
		}
		username = svinst->username;
	}

	if ( svinst->base_dir == NULL ) {
		sieve_storage_set_critical(storage,
			"BUG: Sieve interpreter is initialized without a base_dir");
		*error_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}

	sieve_storage_sys_debug(storage,
		"user=%s, uri=%s", username, uri);

	dstorage->uri = p_strdup(storage->pool, uri);
	dstorage->username = p_strdup(storage->pool, username);

	storage->location = p_strconcat(storage->pool,
		SIEVE_DICT_STORAGE_DRIVER_NAME, ":", storage->location,
		";user=", username, NULL);

	return 0;
}

int sieve_dict_storage_get_dict
(struct sieve_dict_storage *dstorage, struct dict **dict_r,
	enum sieve_error *error_r)
{
	struct sieve_storage *storage = &dstorage->storage;
	struct sieve_instance *svinst = storage->svinst;
	const char *error;
	int ret;

	if ( dstorage->dict == NULL ) {
		ret = dict_init(dstorage->uri, DICT_DATA_TYPE_STRING,
			dstorage->username, svinst->base_dir, &dstorage->dict, &error);
		if ( ret < 0 ) {
			sieve_storage_set_critical(storage,
				"Failed to initialize dict with data `%s' for user `%s': %s",
				dstorage->uri, dstorage->username, error);
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			return -1;
		}
	}

	*dict_r = dstorage->dict;
	return 0;
}

static void sieve_dict_storage_destroy(struct sieve_storage *storage)
{
	struct sieve_dict_storage *dstorage =
		(struct sieve_dict_storage *)storage;

	if ( dstorage->dict != NULL )
		dict_deinit(&dstorage->dict);
}

/*
 * Script access
 */

static struct sieve_script *sieve_dict_storage_get_script
(struct sieve_storage *storage, const char *name)
{
	struct sieve_dict_storage *dstorage =
		(struct sieve_dict_storage *)storage;
	struct sieve_dict_script *dscript;

	T_BEGIN {
		dscript = sieve_dict_script_init(dstorage, name);
	} T_END;

	return &dscript->script;
}

/*
 * Active script
 */

struct sieve_script *sieve_dict_storage_active_script_open
(struct sieve_storage *storage)
{
	struct sieve_dict_storage *dstorage =
		(struct sieve_dict_storage *)storage;
	struct sieve_dict_script *dscript;

	dscript = sieve_dict_script_init
		(dstorage, storage->script_name);
	if ( sieve_script_open(&dscript->script, NULL) < 0 ) {
		struct sieve_script *script = &dscript->script;
		sieve_script_unref(&script);
		return NULL;
	}

	return &dscript->script;
}

int sieve_dict_storage_active_script_get_name
(struct sieve_storage *storage, const char **name_r)
{
	if ( storage->script_name != NULL )
		*name_r = storage->script_name;
	else
		*name_r = SIEVE_DICT_SCRIPT_DEFAULT;
	return 0;
}

/*
 * Driver definition
 */

const struct sieve_storage sieve_dict_storage = {
	.driver_name = SIEVE_DICT_STORAGE_DRIVER_NAME,
	.version = 0,
	.v = {
		.alloc = sieve_dict_storage_alloc,
		.destroy = sieve_dict_storage_destroy,
		.init = sieve_dict_storage_init,

		.get_script = sieve_dict_storage_get_script,

		.get_script_sequence = sieve_dict_storage_get_script_sequence,
		.script_sequence_next = sieve_dict_script_sequence_next,
		.script_sequence_destroy = sieve_dict_script_sequence_destroy,

		.active_script_get_name = sieve_dict_storage_active_script_get_name,
		.active_script_open = sieve_dict_storage_active_script_open,

		// FIXME: impement management interface
	}
};
