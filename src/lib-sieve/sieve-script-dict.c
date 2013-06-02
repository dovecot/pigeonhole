/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "strfuncs.h"
#include "istream.h"
#include "dict.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-binary.h"

#include "sieve-script-private.h"

struct sieve_dict_script {
	struct sieve_script script;

	struct dict *dict;
	const char *dict_uri;

	pool_t data_pool;
	const char *data_id;
	const char *data;

	const char *binpath;
};

#define DICT_SIEVE_PATH DICT_PATH_PRIVATE"sieve/"
#define DICT_SIEVE_NAME_PATH DICT_SIEVE_PATH"name/"
#define DICT_SIEVE_DATA_PATH DICT_SIEVE_PATH"data/"

#define SIEVE_DICT_SCRIPT_DEFAULT "default"

/*
 * Script file implementation
 */

static struct sieve_script *sieve_dict_script_alloc(void)
{
	struct sieve_dict_script *script;
	pool_t pool;

	pool = pool_alloconly_create("sieve_dict_script", 1024);
	script = p_new(pool, struct sieve_dict_script, 1);
	script->script = sieve_dict_script;
	script->script.pool = pool;

	return &script->script;
}

static void sieve_dict_script_free(struct sieve_script *_script)
{
	struct sieve_dict_script *script = (struct sieve_dict_script *)_script;

	if ( script->dict != NULL )
		dict_deinit(&script->dict);

	if ( script->data_pool != NULL )
		pool_unref(&script->data_pool);
}

static int sieve_dict_script_open
(struct sieve_script *_script, const char *data, const char *const *options,
	enum sieve_error *error_r)
{
	struct sieve_dict_script *script = (struct sieve_dict_script *)_script;
	struct sieve_instance *svinst = _script->svinst;
	struct sieve_error_handler *ehandler = _script->ehandler;
	const char *username = NULL, *name = _script->name;
	const char *path, *error;
	int ret;

	if ( options != NULL ) {
		while ( *options != NULL ) {
			const char *option = *options;

			if ( strncasecmp(option, "user=", 5) == 0 && option[5] != '\0' ) {
				username = option+5;
			} else {
				sieve_critical(svinst, ehandler, NULL, "failed to open sieve script",
					"sieve dict backend: invalid option `%s'", option);
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
				return -1;
			}

			options++;
		}
	}

	if ( name == NULL ) {
		name = _script->name = SIEVE_DICT_SCRIPT_DEFAULT;
	}

	if ( username == NULL ) {
		if ( svinst->username == NULL ) {
			sieve_critical(svinst, ehandler, name, "failed to open sieve script",
				"sieve dict backend: no username specified");
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
			return -1;
		}
		username = svinst->username;
	}

	if ( svinst->base_dir == NULL ) {
		sieve_critical(svinst, ehandler, name, "failed to open sieve script",
			"sieve dict backend: BUG: Sieve interpreter is initialized without "
			"a base_dir");
		*error_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}

	if ( _script->svinst->debug ) {
		sieve_sys_debug(_script->svinst, "sieve dict backend: "
			"user=%s, uri=%s, script=%s", username, data, name);
	}

	script->dict_uri = p_strdup(_script->pool, data);
	ret = dict_init(script->dict_uri, DICT_DATA_TYPE_STRING, username,
		svinst->base_dir, &script->dict, &error);
	if ( ret < 0 ) {
		sieve_critical(svinst, ehandler, name, "failed to open sieve script",
			"sieve dict backend: failed to initialize dict with data `%s' "
			"for user `%s': %s", data, username, error);
		*error_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}

	path = t_strconcat
		(DICT_SIEVE_NAME_PATH, dict_escape_string(name), NULL);

	ret = dict_lookup
		(script->dict, script->script.pool, path, &script->data_id);
	if ( ret <= 0 ) {
		if ( ret < 0 ) {
			sieve_critical(svinst, ehandler, name, "failed to open sieve script",
				"sieve dict backend: failed to lookup script id from path %s", path);
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
		} else {
			if ( svinst->debug ) {
				sieve_sys_debug(svinst, "sieve dict backend: "
					"script `%s' not found at path %s", name, path);
			}
			*error_r = SIEVE_ERROR_NOT_FOUND;
		}

		dict_deinit(&script->dict);
		return -1;
	}

	if ( _script->bin_dir != NULL ) {
		script->binpath = p_strconcat(_script->pool, _script->bin_dir, "/",
			sieve_binfile_from_name(name), NULL);
	}

	if ( strcmp(name, SIEVE_DICT_SCRIPT_DEFAULT) == 0 ) {
		_script->location = p_strconcat(_script->pool,
			SIEVE_DICT_SCRIPT_DRIVER_NAME, ":", data, NULL);
	} else {
		_script->location = p_strconcat(_script->pool,
			SIEVE_DICT_SCRIPT_DRIVER_NAME, ":", data, ";name=", name, NULL);
	}

	return 0;
}

static int sieve_dict_script_get_stream
(struct sieve_script *_script, struct istream **stream_r,
	enum sieve_error *error_r)
{
	struct sieve_dict_script *script = (struct sieve_dict_script *)_script;
	struct sieve_instance *svinst = _script->svinst;
	struct sieve_error_handler *ehandler = _script->ehandler;
	const char *path, *name = _script->name;
	int ret;

	script->data_pool =
		pool_alloconly_create("sieve_dict_script data pool", 1024);

	path = t_strconcat
		(DICT_SIEVE_DATA_PATH, dict_escape_string(script->data_id), NULL);

	ret = dict_lookup
		(script->dict, script->data_pool, path, &script->data);
	if ( ret <= 0 ) {
		if ( ret < 0 ) {
			sieve_critical(svinst, ehandler, name, "failed to open sieve script",
				"sieve dict backend: failed to lookup data with id `%s' "
				"for script `%s' from path %s",	script->data_id, name,
				path);
		} else {
			sieve_critical(svinst, ehandler, name, "failed to open sieve script",
				"sieve dict backend: data with id `%s' for script `%s' "
				"not found at path %s",	script->data_id, name, path);
		}
		*error_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}

	*stream_r = i_stream_create_from_data(script->data, strlen(script->data));
	return 0;
}

static int sieve_dict_script_binary_read_metadata
(struct sieve_script *_script, struct sieve_binary_block *sblock,
	sieve_size_t *offset)
{
	struct sieve_dict_script *script = (struct sieve_dict_script *)_script;
	struct sieve_instance *svinst = _script->svinst;
	struct sieve_binary *sbin = sieve_binary_block_get_binary(sblock);
	string_t *data_id;

	if ( !sieve_binary_read_string(sblock, offset, &data_id) ) {
		sieve_sys_error(svinst,
			"sieve dict script: binary %s has invalid metadata for script %s",
			sieve_binary_path(sbin), sieve_script_location(_script));
		return -1;
	}

	if ( strcmp(str_c(data_id), script->data_id) != 0 )
		return 0;

	return 1;
}

static void sieve_dict_script_binary_write_metadata
(struct sieve_script *_script, struct sieve_binary_block *sblock)
{
	struct sieve_dict_script *script = (struct sieve_dict_script *)_script;

	sieve_binary_emit_cstring(sblock, script->data_id);
}

static struct sieve_binary *sieve_dict_script_binary_load
(struct sieve_script *_script, enum sieve_error *error_r)
{
	struct sieve_dict_script *script = (struct sieve_dict_script *)_script;

	if (script->binpath == NULL)
		return NULL;

	return sieve_binary_open(_script->svinst, script->binpath, _script, error_r);
}

static int sieve_dict_script_binary_save
(struct sieve_script *_script, struct sieve_binary *sbin, bool update,
	enum sieve_error *error_r)
{
	struct sieve_dict_script *script = (struct sieve_dict_script *)_script;

	if (script->binpath == NULL)
		return 0;

	if ( sieve_script_setup_bindir(_script, 0700) < 0 )
		return -1;

	return sieve_binary_save(sbin, script->binpath, update, 0600, error_r);
}

static bool sieve_dict_script_equals
(const struct sieve_script *_script, const struct sieve_script *_other)
{
	struct sieve_dict_script *script = (struct sieve_dict_script *)_script;
	struct sieve_dict_script *other = (struct sieve_dict_script *)_other;

	if ( script == NULL || other == NULL )
		return FALSE;

	if ( strcmp(script->dict_uri, other->dict_uri) != 0 )
		return FALSE;

	i_assert( _script->name != NULL && _other->name != NULL );

	return ( strcmp(_script->name, _other->name) == 0 );
}


const struct sieve_script sieve_dict_script = {
	.driver_name = SIEVE_DICT_SCRIPT_DRIVER_NAME,
	.v = {
		sieve_dict_script_alloc,
		sieve_dict_script_free,

		sieve_dict_script_open,

		sieve_dict_script_get_stream,

		sieve_dict_script_binary_read_metadata,
		sieve_dict_script_binary_write_metadata,
		sieve_dict_script_binary_load,
		sieve_dict_script_binary_save,

		NULL,
		sieve_dict_script_equals
	}
};
