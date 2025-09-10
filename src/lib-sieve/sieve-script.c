/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "compat.h"
#include "unichar.h"
#include "str.h"
#include "str-sanitize.h"
#include "hash.h"
#include "array.h"
#include "eacces-error.h"
#include "mkdir-parents.h"
#include "istream.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-error.h"
#include "sieve-dump.h"
#include "sieve-binary.h"

#include "sieve-storage-private.h"
#include "sieve-script-private.h"

/*
 * Script name
 */

bool sieve_script_name_is_valid(const char *scriptname)
{
	ARRAY_TYPE(unichars) uni_name;
	unsigned int count, i;
	const unichar_t *name_chars;
	size_t namelen = strlen(scriptname);

	/* Check minimum length */
	if (namelen == 0)
		return FALSE;

	/* Check worst-case maximum length */
	if (namelen > SIEVE_MAX_SCRIPT_NAME_LEN * 4)
		return FALSE;

	/* Intialize array for unicode characters */
	t_array_init(&uni_name, namelen * 4);

	/* Convert UTF-8 to UCS4/UTF-32 */
	if (uni_utf8_to_ucs4(scriptname, &uni_name) < 0)
		return FALSE;
	name_chars = array_get(&uni_name, &count);

	/* Check true maximum length */
	if (count > SIEVE_MAX_SCRIPT_NAME_LEN)
		return FALSE;

	/* Scan name for invalid characters
	 *   FIXME: compliance with Net-Unicode Definition (Section 2 of
	 *          RFC 5198) is not checked fully and no normalization
	 *          is performed.
	 */
	for (i = 0; i < count; i++) {
		/* 0000-001F; [CONTROL CHARACTERS] */
		if (name_chars[i] <= 0x001f)
			return FALSE;
		/* 002F; SLASH (not RFC-prohibited, but '/' is dangerous) */
		if (name_chars[i] == 0x002f)
			return FALSE;
		/* 007F; DELETE */
		if (name_chars[i] == 0x007f)
			return FALSE;
		/* 0080-009F; [CONTROL CHARACTERS] */
		if (name_chars[i] >= 0x0080 && name_chars[i] <= 0x009f)
			return FALSE;
		/* 00FF */
		if (name_chars[i] == 0x00ff)
			return FALSE;
		/* 2028; LINE SEPARATOR */
		/* 2029; PARAGRAPH SEPARATOR */
		if (name_chars[i] == 0x2028 || name_chars[i] == 0x2029)
			return FALSE;
	}

	return TRUE;
}

/*
 * Script instance
 */

static void sieve_script_update_event(struct sieve_script *script)
{
	if (script->name == NULL)
		event_set_append_log_prefix(script->event, "script: ");
	else {
		event_add_str(script->event, "script_name", script->name);
		event_set_append_log_prefix(
			script->event, t_strdup_printf("script '%s': ",
						       script->name));
	}
}

void sieve_script_init(struct sieve_script *script,
		       struct sieve_storage *storage,
		       const struct sieve_script *script_class,
		       const char *name)
{
	i_assert(storage != NULL);

	script->script_class = script_class;
	script->refcount = 1;
	script->storage = storage;
	script->name = p_strdup_empty(script->pool, name);

	script->event = event_create(storage->event);
	sieve_script_update_event(script);

	sieve_storage_ref(storage);
}

static int
sieve_script_create_common(struct sieve_instance *svinst,
			   const char *cause, const char *type,
			   const char *name, bool open,
			   struct sieve_script **script_r,
			   enum sieve_error *error_code_r,
			   const char **error_r)
{
	struct sieve_storage_sequence *sseq;

	*script_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	if (sieve_storage_sequence_create(svinst, svinst->event, cause, type,
					  &sseq, error_code_r, error_r) < 0)
		return -1;

	struct sieve_storage *storage;
	struct sieve_script *script;
	int ret;

	/* Find the first storage that has the script */
	for (;;) {
		*error_code_r = SIEVE_ERROR_NONE;
		*error_r = NULL;
		ret = sieve_storage_sequence_next(sseq, &storage,
						  error_code_r, error_r);
		if (ret == 0)
			break;
		if (ret < 0) {
			if (*error_code_r == SIEVE_ERROR_NOT_FOUND)
				continue;
			ret = -1;
			break;
		}
		if (sieve_storage_get_script(storage, name,
					     &script, error_code_r) < 0) {
			if (*error_code_r == SIEVE_ERROR_NOT_FOUND) {
				sieve_storage_unref(&storage);
				continue;
			}
			*error_r = sieve_storage_get_last_error(
				storage, error_code_r);
			ret = -1;
		} else {
			ret = 1;
		}
		sieve_storage_unref(&storage);
		if (ret > 0 && open &&
		    sieve_script_open(script, error_code_r) < 0) {
			*error_r = sieve_storage_get_last_error(
				storage, error_code_r);
			sieve_script_unref(&script);
			if (*error_code_r == SIEVE_ERROR_NOT_FOUND)
				continue;
			ret = -1;
		}
		break;
	}

	if (ret > 0) {
		*script_r = script;
		ret = 0;
	} else if (ret == 0) {
		i_assert(*error_code_r == SIEVE_ERROR_NONE);
		sieve_error_create_script_not_found(
			name, error_code_r, error_r);
		ret = -1;
	}

	sieve_storage_sequence_free(&sseq);
	return ret;
}

int sieve_script_create(struct sieve_instance *svinst,
			const char *cause, const char *type, const char *name,
			struct sieve_script **script_r,
			enum sieve_error *error_code_r, const char **error_r)
{
	return sieve_script_create_common(svinst, cause, type, name, FALSE,
					  script_r, error_code_r, error_r);
}

int sieve_script_create_in(struct sieve_instance *svinst, const char *cause,
			   const char *storage_name, const char *name,
			   struct sieve_script **script_r,
			   enum sieve_error *error_code_r,
			   const char **error_r)
{
	struct sieve_storage *storage;
	int ret;

	*script_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	if (sieve_storage_create(svinst, svinst->event, cause, storage_name, 0,
				 &storage, error_code_r, error_r) < 0)
		return -1;
	ret = sieve_storage_get_script_direct(storage, name, script_r, NULL);
	if (ret < 0)
		*error_r = sieve_storage_get_last_error(storage, error_code_r);
	sieve_storage_unref(&storage);
	return ret;
}

void sieve_script_ref(struct sieve_script *script)
{
	script->refcount++;
}

void sieve_script_unref(struct sieve_script **_script)
{
	struct sieve_script *script = *_script;

	if (script == NULL)
		return;
	*_script = NULL;

	i_assert(script->refcount > 0);
	if (--script->refcount != 0)
		return;

	if (script->stream != NULL) {
		struct event_passthrough *e =
			event_create_passthrough(script->event)->
			set_name("sieve_script_closed");
		e_debug(e->event(), "Closed script");
	}
	i_stream_unref(&script->stream);

	if (script->v.destroy != NULL)
		script->v.destroy(script);

	sieve_storage_unref(&script->storage);
	event_unref(&script->event);
	pool_unref(&script->pool);
}

int sieve_script_open(struct sieve_script *script,
		      enum sieve_error *error_code_r)
{
	struct sieve_storage *storage = script->storage;
	int ret;

	sieve_error_args_init(&error_code_r, NULL);
	sieve_storage_clear_error(storage);

	if (script->open)
		return 0;

	ret = script->v.open(script);
	i_assert(ret <= 0);
	if (ret < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
		*error_code_r = storage->error_code;
		return -1;
	}

	i_assert(script->name != NULL);
	script->open = TRUE;

	sieve_script_update_event(script);
	e_debug(script->event, "Opened from '%s'", storage->name);
	return 0;
}

int sieve_script_open_as(struct sieve_script *script, const char *name,
			 enum sieve_error *error_code_r)
{
	if (sieve_script_open(script, error_code_r) < 0)
		return -1;

	/* override name */
	i_assert(name != NULL && *name != '\0');
	script->name = p_strdup(script->pool, name);
	sieve_script_update_event(script);
	return 0;
}

int sieve_script_create_open(struct sieve_instance *svinst,
			     const char *cause, const char *type,
			     const char *name, struct sieve_script **script_r,
			     enum sieve_error *error_code_r,
			     const char **error_r)
{
	return sieve_script_create_common(svinst, cause, type, name, TRUE,
					  script_r, error_code_r, error_r);
}

int sieve_script_create_open_in(struct sieve_instance *svinst,
				const char *cause,
				const char *storage_name, const char *name,
				struct sieve_script **script_r,
				enum sieve_error *error_code_r,
				const char **error_r)
{
	struct sieve_script *script;

	*script_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	if (sieve_script_create_in(svinst, cause, storage_name, name,
				   &script, error_code_r, error_r) < 0)
		return -1;

	if (sieve_script_open(script, NULL) < 0) {
		*error_r = sieve_script_get_last_error(script, error_code_r);
		sieve_script_unref(&script);
		return -1;
	}

	*script_r = script;
	return 0;
}

int sieve_script_check(struct sieve_instance *svinst,
		       const char *cause, const char *type, const char *name,
		       enum sieve_error *error_code_r, const char **error_r)
{
	struct sieve_script *script;
	enum sieve_error error_code;

	sieve_error_args_init(&error_code_r, &error_r);

	if (sieve_script_create_open(svinst, cause, type, name,
				     &script, &error_code, error_r) < 0)
		return (*error_code_r == SIEVE_ERROR_NOT_FOUND ? 0 : -1);

	sieve_script_unref(&script);
	return 1;
}

/*
 * Properties
 */

const char *sieve_script_name(const struct sieve_script *script)
{
	return script->name;
}

const char *sieve_script_label(const struct sieve_script *script)
{
	if (*script->name == '\0')
		return script->storage->name;
	return t_strconcat(script->storage->name, "/", script->name, NULL);
}

const char *sieve_script_storage_type(const struct sieve_script *script)
{
	return script->storage->type;
}

const char *sieve_script_cause(const struct sieve_script *script)
{
	return script->storage->cause;
}

struct sieve_instance *sieve_script_svinst(const struct sieve_script *script)
{
	return script->storage->svinst;
}

int sieve_script_get_size(struct sieve_script *script, uoff_t *size_r)
{
	struct istream *stream;
	int ret;

	if (script->v.get_size != NULL) {
		if ((ret = script->v.get_size(script, size_r)) != 0)
			return ret;
	}

	/* Try getting size from the stream */
	if (script->stream == NULL &&
	    sieve_script_get_stream(script, &stream, NULL) < 0)
		return -1;

	if (i_stream_get_size(script->stream, TRUE, size_r) < 0) {
		sieve_storage_set_critical(script->storage,
			"i_stream_get_size(%s) failed: %s",
			i_stream_get_name(script->stream),
			i_stream_get_error(script->stream));
		return -1;
	}
	return 0;
}

bool sieve_script_is_open(const struct sieve_script *script)
{
	return script->open;
}

bool sieve_script_is_default(const struct sieve_script *script)
{
	return script->storage->is_default;
}

/*
 * Stream management
 */

int sieve_script_get_stream(struct sieve_script *script,
			    struct istream **stream_r,
			    enum sieve_error *error_code_r)
{
	struct sieve_storage *storage = script->storage;
	int ret;

	sieve_error_args_init(&error_code_r, NULL);
	sieve_storage_clear_error(storage);

	if (script->stream != NULL) {
		*stream_r = script->stream;
		return 0;
	}

	// FIXME: necessary?
	i_assert(script->open);

	T_BEGIN {
		ret = script->v.get_stream(script, &script->stream);
	} T_END;

	if (ret < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
		*error_code_r = storage->error_code;

		struct event_passthrough *e =
			event_create_passthrough(script->event)->
			add_str("error", storage->error)->
			set_name("sieve_script_opened");
		e_debug(e->event(), "Failed to open script for reading: %s",
			storage->error);
		return -1;
	}

	struct event_passthrough *e =
		event_create_passthrough(script->event)->
		set_name("sieve_script_opened");
	e_debug(e->event(), "Opened script for reading");

	*stream_r = script->stream;
	return 0;
}

/*
 * Comparison
 */

int sieve_script_cmp(const struct sieve_script *script1,
		     const struct sieve_script *script2)
{
	int ret;

	if (script1 == script2)
		return 0;
	if (script1 == NULL || script2 == NULL)
		return (script1 == NULL ? -1 : 1);
	if (script1->script_class != script2->script_class)
		return (script1->script_class > script2->script_class ? 1 : -1);

	if (script1->v.cmp == NULL) {
		ret = sieve_storage_cmp(script1->storage, script2->storage);
		if (ret != 0)
			return (ret < 0 ? -1 : 1);

		return null_strcmp(script1->name, script2->name);
	}

	return script1->v.cmp(script1, script2);
}

unsigned int sieve_script_hash(const struct sieve_script *script)
{
	if (script == NULL)
		return 0;

	unsigned int hash = 0;

	hash ^= POINTER_CAST_TO(script->script_class, unsigned int);
	hash ^= sieve_storage_hash(script->storage);
	hash ^= str_hash(script->name);

	return hash;
}

/*
 * Binary
 */

int sieve_script_binary_read_metadata(struct sieve_script *script,
				      struct sieve_binary_block *sblock,
				      sieve_size_t *offset)
{
	struct sieve_binary *sbin = sieve_binary_block_get_binary(sblock);
	string_t *storage_class, *storage_name, *name;
	unsigned int version;

	if ((sieve_binary_block_get_size(sblock) - *offset) == 0)
		return 0;

	/* storage class */
	if (!sieve_binary_read_string(sblock, offset, &storage_class)) {
		e_error(script->event,
			"Binary '%s' has invalid metadata for script '%s': "
			"Invalid storage class",
			sieve_binary_path(sbin), sieve_script_label(script));
		return -1;
	}
	if (strcmp(str_c(storage_class), script->driver_name) != 0) {
		e_debug(script->event,
			"Binary '%s' reports unexpected driver name for script '%s' "
			"('%s' rather than '%s')",
			sieve_binary_path(sbin), sieve_script_label(script),
			str_c(storage_class), script->driver_name);
		return 0;
	}

	/* version */
	if (!sieve_binary_read_unsigned(sblock, offset, &version)) {
		e_error(script->event,
			"Binary '%s' has invalid metadata for script '%s': "
			"Invalid version",
			sieve_binary_path(sbin), sieve_script_label(script));
		return -1;
	}
	if (script->storage->version != version) {
		e_debug(script->event,
			"Binary '%s' was compiled with "
			"a different version of the '%s' script storage class "
			"(compiled v%d, expected v%d; "
				"automatically fixed when re-compiled)",
			sieve_binary_path(sbin), script->driver_name,
		 	version, script->storage->version);
		return 0;
	}

	/* storage */
	if (!sieve_binary_read_string(sblock, offset, &storage_name)) {
		e_error(script->event,
			"Binary '%s' has invalid metadata for script '%s': "
			"Invalid storage name",
			sieve_binary_path(sbin), sieve_script_label(script));
		return -1;
	}
	if (str_len(storage_name) > 0 &&
	    strcmp(str_c(storage_name), script->storage->name) != 0) {
		e_debug(script->event,
			"Binary '%s' reports different storage "
			"for script '%s' (binary points to '%s')",
			sieve_binary_path(sbin), sieve_script_label(script),
			str_c(storage_name));
		return 0;
	}

	/* name */
	if (!sieve_binary_read_string(sblock, offset, &name)) {
		e_error(script->event,
			"Binary '%s' has invalid metadata for script '%s': "
			"Invalid script name",
			sieve_binary_path(sbin), sieve_script_label(script));
		return -1;
	}
	if (str_len(name) > 0 && strcmp(str_c(name), script->name) != 0) {
		e_debug(script->event,
			"Binary '%s' reports different script name "
			"for script '%s' (binary points to '%s/%s')",
			sieve_binary_path(sbin), sieve_script_label(script),
			str_c(storage_name), str_c(name));
		return 0;
	}

	if (script->v.binary_read_metadata == NULL)
		return 1;

	return script->v.binary_read_metadata(script, sblock, offset);
}

void sieve_script_binary_write_metadata(struct sieve_script *script,
					struct sieve_binary_block *sblock)
{
	struct sieve_binary *sbin = sieve_binary_block_get_binary(sblock);
	struct sieve_instance *svinst = sieve_binary_svinst(sbin);

	sieve_binary_emit_cstring(sblock, script->driver_name);
	sieve_binary_emit_unsigned(sblock, script->storage->version);

	if (HAS_ALL_BITS(svinst->flags, SIEVE_FLAG_COMMAND_LINE)) {
		sieve_binary_emit_cstring(sblock, "");
		sieve_binary_emit_cstring(sblock, "");
	} else {
		sieve_binary_emit_cstring(sblock, script->storage->name);
		sieve_binary_emit_cstring(sblock, script->name);
	}

	if (script->v.binary_write_metadata == NULL)
		return;

	script->v.binary_write_metadata(script, sblock);
}

bool sieve_script_binary_dump_metadata(struct sieve_script *script,
				       struct sieve_dumptime_env *denv,
				       struct sieve_binary_block *sblock,
				       sieve_size_t *offset)
{
	struct sieve_binary *sbin = sieve_binary_block_get_binary(sblock);
	struct sieve_instance *svinst = sieve_binary_svinst(sbin);
	string_t *storage_class, *storage_name, *name;
	struct sieve_script *adhoc_script = NULL;
	unsigned int version;
	bool result = TRUE;

	/* storage class */
	if (!sieve_binary_read_string(sblock, offset, &storage_class))
		return FALSE;
	sieve_binary_dumpf(denv, "class = %s\n", str_c(storage_class));

	/* version */
	if (!sieve_binary_read_unsigned(sblock, offset, &version))
		return FALSE;
	sieve_binary_dumpf(denv, "class.version = %d\n", version);

	/* storage */
	if (!sieve_binary_read_string(sblock, offset, &storage_name))
		return FALSE;
	if (str_len(storage_name) == 0)
		sieve_binary_dumpf(denv, "storage = (unavailable)\n");
	else
		sieve_binary_dumpf(denv, "storage = %s\n", str_c(storage_name));

	/* name */
	if (!sieve_binary_read_string(sblock, offset, &name))
		return FALSE;
	if (str_len(name) == 0)
		sieve_binary_dumpf(denv, "name = (unavailable)\n");
	else
		sieve_binary_dumpf(denv, "name = %s\n", str_c(name));

	if (script == NULL) {
		adhoc_script = NULL;
		if (sieve_script_create_in(svinst, SIEVE_SCRIPT_CAUSE_ANY,
					   str_c(storage_name), str_c(name),
					   &script, NULL, NULL) == 0)
			adhoc_script = script;
	}

	if (script != NULL && script->v.binary_dump_metadata != NULL) {
		result = script->v.binary_dump_metadata(
			script, denv, sblock, offset);
	}

	sieve_script_unref(&adhoc_script);
	return result;
}

int sieve_script_binary_load_default(struct sieve_script *script,
				     const char *path,
				     struct sieve_binary **sbin_r)
{
	struct sieve_instance *svinst = script->storage->svinst;
	enum sieve_error error_code;

	if (path == NULL) {
		sieve_script_set_error(
			script, SIEVE_ERROR_NOT_POSSIBLE,
			"Cannot load script binary for this storage");
		return -1;
	}

	if (sieve_binary_open(svinst, path, script, sbin_r, &error_code) < 0) {
		sieve_script_set_error(script, error_code,
				       "Failed to load script binary");
		return -1;
	}
	return 0;
}

int sieve_script_binary_load(struct sieve_script *script,
			     struct sieve_binary **sbin_r,
			     enum sieve_error *error_code_r)
{
	struct sieve_storage *storage = script->storage;
	int ret;

	*sbin_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);
	sieve_storage_clear_error(storage);

	if (script->v.binary_load == NULL) {
		sieve_script_set_error(
			script, SIEVE_ERROR_NOT_POSSIBLE,
			"Cannot load script binary for this storage type");
		ret = -1;
	} else {
		ret = script->v.binary_load(script, sbin_r);
		i_assert(ret <= 0);
		i_assert(ret < 0 || *sbin_r != NULL);
	}

	if (ret < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
		*error_code_r = script->storage->error_code;
		return -1;
	}
	return 0;
}

int sieve_script_binary_save_default(struct sieve_script *script ATTR_UNUSED,
				     struct sieve_binary *sbin,
				     const char *path, bool update,
				     mode_t save_mode)
{
	struct sieve_storage *storage = script->storage;
	enum sieve_error error_code;
	int ret;

	if (path == NULL) {
		e_debug(script->event, "No path to save Sieve script");
		sieve_script_set_error(
			script, SIEVE_ERROR_NOT_POSSIBLE,
			"Cannot save script binary for this storage");
		return -1;
	}

	if (storage->bin_path != NULL &&
	    str_begins_with(path, storage->bin_path) &&
	    sieve_storage_setup_bin_path(
		script->storage, mkdir_get_executable_mode(save_mode)) < 0)
		return -1;

	e_debug(script->event, "Saving binary to '%s'", path);

	ret = sieve_binary_save(sbin, path, update, save_mode, &error_code);
	if (ret < 0) {
		sieve_script_set_error(script, error_code,
				       "Failed to save script binary");
		return -1;
	}
	return 0;
}

int sieve_script_binary_save(struct sieve_script *script,
			     struct sieve_binary *sbin, bool update,
			     enum sieve_error *error_code_r)
{
	struct sieve_storage *storage = script->storage;
	struct sieve_script *bin_script = sieve_binary_script(sbin);
	int ret;

	sieve_error_args_init(&error_code_r, NULL);
	sieve_storage_clear_error(storage);

	i_assert(bin_script == NULL || sieve_script_equals(bin_script, script));

	if (script->v.binary_save == NULL) {
		sieve_script_set_error(
			script, SIEVE_ERROR_NOT_POSSIBLE,
			"Cannot save script binary for this storage type");
		ret = -1;
	} else {
		ret = script->v.binary_save(script, sbin, update);
	}

	if (ret < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
		*error_code_r = script->storage->error_code;
		return -1;
	}
	return 0;
}

const char *sieve_script_binary_get_prefix(struct sieve_script *script)
{
	struct sieve_storage *storage = script->storage;

	if (storage->bin_path != NULL &&
	    sieve_storage_setup_bin_path(storage, 0700) >= 0)
		return t_strconcat(storage->bin_path, "/", script->name, NULL);
	if (script->v.binary_get_prefix == NULL)
		return NULL;

	return script->v.binary_get_prefix(script);
}

/*
 * Management
 */

static int
sieve_script_copy_from_default(struct sieve_script *script, const char *newname)
{
	struct sieve_storage *storage = script->storage;
	struct istream *input;
	int ret;

	/* copy from default */
	if ((ret = sieve_script_open(script, NULL)) < 0 ||
	    (ret = sieve_script_get_stream(script, &input, NULL)) < 0) {
		sieve_storage_copy_error(storage->default_storage_for, storage);
		return ret;
	}

	ret = sieve_storage_save_as(storage->default_storage_for,
				    input, newname);
	if (ret < 0) {
		sieve_storage_copy_error(storage, storage->default_storage_for);
	} else if (sieve_script_is_active(script) > 0) {
		struct sieve_script *newscript;
		enum sieve_error error_code;

		if (sieve_storage_open_script(storage->default_storage_for,
					      newname, &newscript,
					      &error_code) < 0) {
			/* Somehow not actually saved */
			ret = (error_code == SIEVE_ERROR_NOT_FOUND ? 0 : -1);
		} else if (sieve_script_activate(newscript, (time_t)-1) < 0) {
			/* Failed to activate; roll back */
			ret = -1;
			(void)sieve_script_delete(newscript, TRUE);
		}
		sieve_script_unref(&newscript);

		if (ret < 0) {
			e_error(storage->event,
				"Failed to implicitly activate script '%s' "
				"after rename",	newname);
			sieve_storage_copy_error(storage->default_storage_for,
						 storage);
		}
	}

	return ret;
}

int sieve_script_rename(struct sieve_script *script, const char *newname)
{
	struct sieve_storage *storage = script->storage;
	const char *oldname = script->name;
	struct event_passthrough *event;
	int ret;

	i_assert(newname != NULL);
	sieve_storage_clear_error(storage);

	/* Check script name */
	if (!sieve_script_name_is_valid(newname)) {
		sieve_script_set_error(script,
			SIEVE_ERROR_BAD_PARAMS,
			"Invalid new Sieve script name '%s'.",
			str_sanitize(newname, 80));
		return -1;
	}

	i_assert(script->open); // FIXME: auto-open?

	if (storage->default_storage_for == NULL) {
		i_assert((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0);

		/* rename script */
		i_assert(script->v.rename != NULL);
		ret = script->v.rename(script, newname);

		/* rename INBOX mailbox attribute */
		if (ret >= 0 && oldname != NULL) {
			(void)sieve_storage_sync_script_rename(storage, oldname,
							       newname);
		}
	} else if (sieve_storage_check_script(storage->default_storage_for,
					      newname, NULL) > 0) {
		sieve_script_set_error(script, SIEVE_ERROR_EXISTS,
			"A sieve script with that name already exists.");
		sieve_storage_copy_error(storage->default_storage_for, storage);
		ret = -1;
	} else {
		ret = sieve_script_copy_from_default(script, newname);
	}

	event = event_create_passthrough(script->event)->
		clear_field("script_name")->
		add_str("old_script_name", script->name)->
		add_str("new_script_name", newname)->
		set_name("sieve_script_renamed");

	if (ret >= 0) {
		e_debug(event->event(), "Script renamed to '%s'", newname);
		sieve_script_update_event(script);
	} else {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
		event = event->add_str("error", storage->error);

		e_debug(event->event(), "Failed to rename script: %s",
			storage->error);
	}

	return ret;
}

int sieve_script_delete(struct sieve_script *script, bool ignore_active)
{
	struct sieve_storage *storage = script->storage;
	bool is_active = FALSE;
	int ret = 0;

	i_assert(script->open); // FIXME: auto-open?
	sieve_storage_clear_error(storage);

	/* Is the requested script active? */
	if (sieve_script_is_active(script) > 0) {
		is_active = TRUE;
		if (!ignore_active) {
			sieve_script_set_error(script, SIEVE_ERROR_ACTIVE,
				"Cannot delete the active Sieve script.");
			if (storage->default_storage_for != NULL) {
				sieve_storage_copy_error(
					storage->default_storage_for, storage);
			}
			return -1;
		}
	}

	/* Trying to delete the default script? */
	if (storage->is_default) {
		/* ignore */
		return 0;
	}

	i_assert((script->storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0);

	/* Deactivate it explicity */
	if (ignore_active && is_active)
		(void)sieve_storage_deactivate(storage, (time_t)-1);

	i_assert(script->v.delete != NULL);
	ret = script->v.delete(script);

	if (ret >= 0) {
		struct event_passthrough *e =
			event_create_passthrough(script->event)->
			set_name("sieve_script_deleted");
		e_debug(e->event(), "Script deleted");

		/* unset INBOX mailbox attribute */
		(void)sieve_storage_sync_script_delete(storage, script->name);
	} else {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);

		struct event_passthrough *e =
			event_create_passthrough(script->event)->
			add_str("error", storage->error)->
			set_name("sieve_script_deleted");
		e_debug(e->event(), "Failed to delete script: %s",
			storage->error);
	}
	return ret;
}

int sieve_script_is_active(struct sieve_script *script)
{
	struct sieve_storage *storage = script->storage;
	int ret;

	sieve_storage_clear_error(storage);

	/* Special handling if this is a default script */
	if (storage->default_storage_for != NULL) {
		ret = sieve_storage_active_script_is_default(
			storage->default_storage_for);
		if (ret < 0) {
			sieve_storage_copy_error(storage,
						 storage->default_storage_for);
			i_assert(storage->error_code != SIEVE_ERROR_NONE);
			i_assert(storage->error != NULL);
		}
		return ret;
	}

	if (script->v.is_active == NULL)
		return 0;
	ret = script->v.is_active(script);
	i_assert(ret >= 0 ||
		 (storage->error_code != SIEVE_ERROR_NONE &&
		  storage->error != NULL));
	return ret;
}

int sieve_script_activate(struct sieve_script *script, time_t mtime)
{
	struct sieve_storage *storage = script->storage;
	int ret = 0;

	i_assert(script->open); // FIXME: auto-open?
	sieve_storage_clear_error(storage);

	if (storage->default_storage_for == NULL) {
		i_assert((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0);

		i_assert(script->v.activate != NULL);
		ret = script->v.activate(script);

		if (ret >= 0) {
			struct event_passthrough *e =
				event_create_passthrough(script->event)->
				set_name("sieve_script_activated");
			e_debug(e->event(), "Script activated");

			sieve_storage_set_modified(storage, mtime);
			(void)sieve_storage_sync_script_activate(storage);
		} else {
			i_assert(storage->error_code != SIEVE_ERROR_NONE);
			i_assert(storage->error != NULL);

			struct event_passthrough *e =
				event_create_passthrough(script->event)->
				add_str("error", storage->error)->
				set_name("sieve_script_activated");
			e_debug(e->event(), "Failed to activate script: %s",
				storage->error);
		}

	} else {
		/* Activating the default script is equal to deactivating
		   the storage */
		ret = sieve_storage_deactivate(storage->default_storage_for,
					       (time_t)-1);
		if (ret < 0) {
			sieve_storage_copy_error(storage,
						 storage->default_storage_for);
		}
	}

	return ret;
}

/*
 * Error handling
 */

void sieve_script_set_error(struct sieve_script *script,
			    enum sieve_error error_code, const char *fmt, ...)
{
	struct sieve_storage *storage = script->storage;
	va_list va;

	sieve_storage_clear_error(storage);

	if (fmt != NULL) {
		va_start(va, fmt);
		storage->error = i_strdup_vprintf(fmt, va);
		va_end(va);
	}
	storage->error_code = error_code;
}

void sieve_script_set_internal_error(struct sieve_script *script)
{
	sieve_storage_set_internal_error(script->storage);
}

void sieve_script_set_critical(struct sieve_script *script,
			       const char *fmt, ...)
{
	struct sieve_storage *storage = script->storage;

	va_list va;

	if (fmt != NULL) {
		if ((storage->flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) == 0) {
			va_start(va, fmt);
			e_error(script->event, "%s", t_strdup_vprintf(fmt, va));
			va_end(va);

			sieve_storage_set_internal_error(storage);

		} else {
			sieve_storage_clear_error(storage);

			/* no user is involved while synchronizing, so do it the
			   normal way */
			va_start(va, fmt);
			storage->error = i_strdup_vprintf(fmt, va);
			va_end(va);

			storage->error_code = SIEVE_ERROR_TEMP_FAILURE;
		}
	}
}

void sieve_script_set_not_found_error(struct sieve_script *script,
				      const char *name)
{
	name = (name == NULL || *name == '\0' ? script->name : name);
	sieve_storage_set_not_found_error(script->storage, name);
}

const char *
sieve_script_get_last_error(struct sieve_script *script,
			    enum sieve_error *error_code_r)
{
	return sieve_storage_get_last_error(script->storage, error_code_r);
}

const char *sieve_script_get_last_error_lcase(struct sieve_script *script)
{
	return sieve_error_from_external(script->storage->error);
}

/*
 * Script sequence
 */

int sieve_script_sequence_create(struct sieve_instance *svinst,
				 struct event *event_parent,
				 const char *cause, const char *type,
				 struct sieve_script_sequence **sseq_r,
				 enum sieve_error *error_code_r,
				 const char **error_r)
{
	struct sieve_storage_sequence *storage_seq;
	struct sieve_script_sequence *sseq;

	*sseq_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	if (sieve_storage_sequence_create(svinst, event_parent,
					  cause, type, &storage_seq,
					  error_code_r, error_r) < 0)
		return -1;

	sseq = i_new(struct sieve_script_sequence, 1);
	sseq->storage_seq = storage_seq;

	*sseq_r = sseq;
	return 0;
}

static int
sieve_script_sequence_init_storage(struct sieve_script_sequence *sseq,
				   enum sieve_error *error_code_r,
				   const char **error_r)
{
	int ret;

	while (sseq->storage == NULL) {
		ret = sieve_storage_sequence_next(sseq->storage_seq,
						  &sseq->storage,
						  error_code_r, error_r);
		if (ret == 0)
			return 0;
		if (ret < 0) {
			if (*error_code_r == SIEVE_ERROR_NOT_FOUND)
				continue;
			return -1;
		}

		struct sieve_storage *storage =	sseq->storage;

		i_assert(storage->v.script_sequence_init != NULL);
		sieve_storage_clear_error(storage);
		ret = storage->v.script_sequence_init(sseq);
		if (ret < 0) {
			i_assert(storage->error_code != SIEVE_ERROR_NONE);
			i_assert(storage->error != NULL);
			*error_code_r = storage->error_code;
			*error_r = storage->error;
			sieve_storage_unref(&sseq->storage);
			if (*error_code_r != SIEVE_ERROR_NOT_FOUND)
				return -1;
		}
	}
	return 1;
}

static void
sieve_script_sequence_deinit_storage(struct sieve_script_sequence *sseq)
{
	struct sieve_storage *storage =	sseq->storage;

	if (storage != NULL && storage->v.script_sequence_destroy != NULL)
		storage->v.script_sequence_destroy(sseq);
	sseq->storage_data = NULL;

	sieve_storage_unref(&sseq->storage);
}

int sieve_script_sequence_next(struct sieve_script_sequence *sseq,
			       struct sieve_script **script_r,
			       enum sieve_error *error_code_r,
			       const char **error_r)
{
	int ret;

	*script_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	while ((ret = sieve_script_sequence_init_storage(
			sseq, error_code_r, error_r)) > 0) {
		struct sieve_storage *storage =	sseq->storage;

		i_assert(storage->v.script_sequence_next != NULL);
		sieve_storage_clear_error(storage);
		ret = storage->v.script_sequence_next(sseq, script_r);
		if (ret > 0)
			break;

		if (ret < 0) {
			i_assert(storage->error_code != SIEVE_ERROR_NONE);
			i_assert(storage->error != NULL);

			if (storage->error_code == SIEVE_ERROR_NOT_FOUND)
				ret = 0;
			else {
				*error_code_r = storage->error_code;
				*error_r = t_strdup(storage->error);
			}
		}

		sieve_script_sequence_deinit_storage(sseq);
		if (ret < 0)
			break;
	}

	return ret;
}

void sieve_script_sequence_free(struct sieve_script_sequence **_sseq)
{
	struct sieve_script_sequence *sseq = *_sseq;

	if (sseq == NULL)
		return;
	*_sseq = NULL;

	sieve_script_sequence_deinit_storage(sseq);
	sieve_storage_sequence_free(&sseq->storage_seq);
	i_free(sseq);
}
