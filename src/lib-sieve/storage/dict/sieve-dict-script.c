/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "strfuncs.h"
#include "istream.h"
#include "dict.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-dump.h"
#include "sieve-binary.h"

#include "sieve-dict-storage.h"

/*
 * Script dict implementation
 */

static struct sieve_dict_script *sieve_dict_script_alloc(void)
{
	struct sieve_dict_script *dscript;
	pool_t pool;

	pool = pool_alloconly_create("sieve_dict_script", 1024);
	dscript = p_new(pool, struct sieve_dict_script, 1);
	dscript->script = sieve_dict_script;
	dscript->script.pool = pool;

	return dscript;
}

struct sieve_dict_script *
sieve_dict_script_init(struct sieve_dict_storage *dstorage, const char *name)
{
	struct sieve_storage *storage = &dstorage->storage;
	struct sieve_dict_script *dscript = NULL;
	const char *location;

	if (name == NULL) {
		name = SIEVE_DICT_SCRIPT_DEFAULT;
		location = storage->location;
	} else {
		location = t_strconcat(storage->location, ";name=", name, NULL);
	}

	dscript = sieve_dict_script_alloc();
	sieve_script_init(&dscript->script, storage, &sieve_dict_script,
			  location, name);

	return dscript;
}

static void sieve_dict_script_destroy(struct sieve_script *script)
{
	struct sieve_dict_script *dscript =
		container_of(script, struct sieve_dict_script, script);

	if (dscript->data_pool != NULL)
		pool_unref(&dscript->data_pool);
}

static int
sieve_dict_script_open(struct sieve_script *script,
		       enum sieve_error *error_code_r)
{
	struct sieve_storage *storage = script->storage;
	struct sieve_instance *svinst = storage->svinst;
	struct sieve_dict_script *dscript =
		container_of(script, struct sieve_dict_script, script);
	struct sieve_dict_storage *dstorage =
		container_of(storage, struct sieve_dict_storage, storage);
	const char *name = script->name;
	const char *path, *data_id, *error;
	int ret;

	if (sieve_dict_storage_get_dict(dstorage, &dscript->dict,
					error_code_r) < 0)
		return -1;

	path = t_strconcat(DICT_SIEVE_NAME_PATH,
			   dict_escape_string(name), NULL);

	struct dict_op_settings set = {
		.username = svinst->username,
	};
	ret = dict_lookup(dscript->dict, &set, script->pool, path,
			  &data_id, &error);
	if (ret <= 0) {
		if (ret < 0) {
			sieve_script_set_critical(script,
				"Failed to lookup script id from path %s: %s",
				path, error);
			*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		} else {
			e_debug(script->event,
				"Script '%s' not found at path %s", name, path);
			sieve_script_set_error(script, SIEVE_ERROR_NOT_FOUND,
					       "Sieve script '%s' not found",
					       name);
			*error_code_r = SIEVE_ERROR_NOT_FOUND;
		}
		return -1;
	}

	dscript->data_id = p_strdup(script->pool, data_id);
	return 0;
}

static int
sieve_dict_script_get_stream(struct sieve_script *script,
			     struct istream **stream_r,
			     enum sieve_error *error_code_r)
{
	struct sieve_storage *storage = script->storage;
	struct sieve_instance *svinst = storage->svinst;
	struct sieve_dict_script *dscript =
		container_of(script, struct sieve_dict_script, script);
	const char *path, *name = script->name, *data, *error;
	int ret;

	dscript->data_pool =
		pool_alloconly_create("sieve_dict_script data pool", 1024);

	path = t_strconcat(DICT_SIEVE_DATA_PATH,
			   dict_escape_string(dscript->data_id), NULL);

	struct dict_op_settings set = {
		.username = svinst->username,
	};
	ret = dict_lookup(dscript->dict, &set, dscript->data_pool, path,
			  &data, &error);
	if (ret <= 0) {
		if (ret < 0) {
			sieve_script_set_critical(script,
				"Failed to lookup data with id '%s' "
				"for script '%s' from path %s: %s",
				dscript->data_id, name, path, error);
		} else {
			sieve_script_set_critical(script,
				"Data with id '%s' for script '%s' not found at path %s",
				dscript->data_id, name, path);
		}
		*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}

	dscript->data = p_strdup(script->pool, data);
	*stream_r = i_stream_create_from_data(dscript->data,
					      strlen(dscript->data));
	return 0;
}

static int
sieve_dict_script_binary_read_metadata(struct sieve_script *script,
				       struct sieve_binary_block *sblock,
				       sieve_size_t *offset)
{
	struct sieve_dict_script *dscript =
		container_of(script, struct sieve_dict_script, script);
	struct sieve_binary *sbin = sieve_binary_block_get_binary(sblock);
	string_t *data_id;

	if (dscript->data_id == NULL && sieve_script_open(script, NULL) < 0)
		return 0;

	if (!sieve_binary_read_string(sblock, offset, &data_id)) {
		e_error(script->event,
			"Binary '%s' has invalid metadata for script '%s'",
			sieve_binary_path(sbin), sieve_script_location(script));
		return -1;
	}
	i_assert(dscript->data_id != NULL);
	if (strcmp(str_c(data_id), dscript->data_id) != 0) {
		e_debug(script->event,
			"Binary '%s' reports different data ID for script '%s' "
			"('%s' rather than '%s')",
			sieve_binary_path(sbin), sieve_script_location(script),
			str_c(data_id), dscript->data_id);
		return 0;
	}
	return 1;
}

static void
sieve_dict_script_binary_write_metadata(struct sieve_script *script,
					struct sieve_binary_block *sblock)
{
	struct sieve_dict_script *dscript =
		container_of(script, struct sieve_dict_script, script);

	sieve_binary_emit_cstring(sblock, dscript->data_id);
}

static bool
sieve_dict_script_binary_dump_metadata(struct sieve_script *script ATTR_UNUSED,
				       struct sieve_dumptime_env *denv,
				       struct sieve_binary_block *sblock,
				       sieve_size_t *offset)
{
	string_t *data_id;

	if (!sieve_binary_read_string(sblock, offset, &data_id))
		return FALSE;

	sieve_binary_dumpf(denv, "dict.data_id = %s\n", str_c(data_id));
	return TRUE;
}

static const char *
sieve_dict_script_get_bin_path(struct sieve_dict_script *dscript)
{
	struct sieve_script *script = &dscript->script;
	struct sieve_storage *storage = script->storage;

	if (dscript->bin_path == NULL) {
		if (storage->bin_path == NULL)
			return NULL;
		dscript->bin_path = p_strconcat(
			script->pool, storage->bin_path, "/",
			sieve_binfile_from_name(script->name), NULL);
	}
	return dscript->bin_path;
}

static int
sieve_dict_script_binary_load(struct sieve_script *script,
			      struct sieve_binary **sbin_r,
			      enum sieve_error *error_code_r)
{
	struct sieve_dict_script *dscript =
		container_of(script, struct sieve_dict_script, script);

	if (sieve_dict_script_get_bin_path(dscript) == NULL)
		return -1;

	return sieve_binary_open(script->storage->svinst, dscript->bin_path,
				 script, sbin_r, error_code_r);
}

static int
sieve_dict_script_binary_save(struct sieve_script *script,
			      struct sieve_binary *sbin, bool update,
			      enum sieve_error *error_code_r)
{
	struct sieve_dict_script *dscript =
		container_of(script, struct sieve_dict_script, script);

	if (sieve_dict_script_get_bin_path(dscript) == NULL)
		return 0;
	if (sieve_storage_setup_bin_path(script->storage, 0700) < 0)
		return -1;

	return sieve_binary_save(sbin, dscript->bin_path, update, 0600,
				 error_code_r);
}

static int
sieve_dict_script_cmp(const struct sieve_script *script,
		      const struct sieve_script *other)
{
	struct sieve_storage *storage = script->storage;
	struct sieve_storage *sother = other->storage;
	int ret;

	ret = strcmp(storage->location, sother->location);
	if (ret != 0)
		return (ret < 0 ? -1 : 1);

	i_assert(script->name != NULL && other->name != NULL);

	ret = strcmp(script->name, other->name);
	if (ret != 0)
		return (ret < 0 ? -1 : 1);
	return 0;
}

const struct sieve_script sieve_dict_script = {
	.driver_name = SIEVE_DICT_STORAGE_DRIVER_NAME,
	.v = {
		.destroy = sieve_dict_script_destroy,

		.open = sieve_dict_script_open,

		.get_stream = sieve_dict_script_get_stream,

		.binary_read_metadata = sieve_dict_script_binary_read_metadata,
		.binary_write_metadata = sieve_dict_script_binary_write_metadata,
		.binary_dump_metadata = sieve_dict_script_binary_dump_metadata,
		.binary_load = sieve_dict_script_binary_load,
		.binary_save = sieve_dict_script_binary_save,

		.cmp = sieve_dict_script_cmp,
	},
};

/*
 * Script sequence
 */

struct sieve_dict_script_sequence {
	struct sieve_script_sequence seq;

	bool done:1;
};

struct sieve_script_sequence *
sieve_dict_storage_get_script_sequence(struct sieve_storage *storage,
				       enum sieve_error *error_code_r)
{
	struct sieve_dict_script_sequence *dseq = NULL;

	if (error_code_r != NULL)
		*error_code_r = SIEVE_ERROR_NONE;

	/* Create sequence object */
	dseq = i_new(struct sieve_dict_script_sequence, 1);
	sieve_script_sequence_init(&dseq->seq, storage);

	return &dseq->seq;
}

int sieve_dict_script_sequence_next(struct sieve_script_sequence *sseq,
				    struct sieve_script **script_r,
				    enum sieve_error *error_code_r)
{
	struct sieve_dict_script_sequence *dseq =
		container_of(sseq, struct sieve_dict_script_sequence, seq);
	struct sieve_dict_storage *dstorage =
		container_of(sseq->storage, struct sieve_dict_storage, storage);
	struct sieve_dict_script *dscript;

	if (dseq->done)
		return 0;
	dseq->done = TRUE;

	dscript = sieve_dict_script_init(dstorage, sseq->storage->script_name);
	if (sieve_script_open(&dscript->script, error_code_r) < 0) {
		struct sieve_script *script = &dscript->script;

		sieve_script_unref(&script);
		return -1;
	}

	*script_r = &dscript->script;
	return 1;
}

void sieve_dict_script_sequence_destroy(struct sieve_script_sequence *sseq)
{
	struct sieve_dict_script_sequence *dseq =
		container_of(sseq, struct sieve_dict_script_sequence, seq);

	i_free(dseq);
}
