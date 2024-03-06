/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "settings.h"
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

static int
sieve_dict_storage_init(struct sieve_storage *storage)
{
	struct sieve_dict_storage *dstorage =
		container_of(storage, struct sieve_dict_storage, storage);
	const char *error;
	int ret;

	struct event *event = event_create(storage->event);
	event_set_ptr(event, SETTINGS_EVENT_FILTER_NAME, "sieve_script_dict");
	ret = dict_init_auto(event, &dstorage->dict, &error);
	event_unref(&event);
	if (ret <= 0) {
		sieve_storage_set_critical(storage,
			"Failed to initialize sieve_script %s dict: %s",
			storage->name, error);
		return -1;
	}

	return 0;
}

static void sieve_dict_storage_destroy(struct sieve_storage *storage)
{
	struct sieve_dict_storage *dstorage =
		container_of(storage, struct sieve_dict_storage, storage);

	dict_deinit(&dstorage->dict);
}

/*
 * Script access
 */

static int
sieve_dict_storage_get_script(struct sieve_storage *storage, const char *name,
			      struct sieve_script **script_r)
{
	struct sieve_dict_storage *dstorage =
		container_of(storage, struct sieve_dict_storage, storage);
	struct sieve_dict_script *dscript;

	T_BEGIN {
		dscript = sieve_dict_script_init(dstorage, name);
	} T_END;

	if (dscript == NULL)
		return -1;
	*script_r = &dscript->script;
	return 0;
}

/*
 * Active script
 */

static int
sieve_dict_storage_active_script_open(struct sieve_storage *storage,
				      struct sieve_script **script_r)
{
	struct sieve_dict_storage *dstorage =
		container_of(storage, struct sieve_dict_storage, storage);
	struct sieve_dict_script *dscript;

	dscript = sieve_dict_script_init(dstorage, storage->script_name);
	if (sieve_script_open(&dscript->script, NULL) < 0) {
		struct sieve_script *script = &dscript->script;
		sieve_script_unref(&script);
		return -1;
	}

	*script_r = &dscript->script;
	return 0;
}

int sieve_dict_storage_active_script_get_name(struct sieve_storage *storage,
					      const char **name_r)
{
	if (storage->script_name != NULL)
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

		.script_sequence_init = sieve_dict_script_sequence_init,
		.script_sequence_next = sieve_dict_script_sequence_next,
		.script_sequence_destroy = sieve_dict_script_sequence_destroy,

		.active_script_get_name = sieve_dict_storage_active_script_get_name,
		.active_script_open = sieve_dict_storage_active_script_open,

		// FIXME: impement management interface
	},
};
