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
#include "istream.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-settings.h"
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

void sieve_script_init(struct sieve_script *script,
		       struct sieve_storage *storage,
		       const struct sieve_script *script_class,
		       const char *location, const char *name)
{
	i_assert(storage != NULL);

	script->script_class = script_class;
	script->refcount = 1;
	script->storage = storage;
	script->location = p_strdup_empty(script->pool, location);
	script->name = p_strdup(script->pool, name);

	script->event = event_create(storage->event);
	event_add_str(script->event, "script_name", name);
	event_add_str(script->event, "script_location", location);
	if (name == NULL)
		event_set_append_log_prefix(script->event, "script: ");
	else {
		event_set_append_log_prefix(
			script->event, t_strdup_printf("script '%s': ", name));
	}

	sieve_storage_ref(storage);
}

struct sieve_script *
sieve_script_create(struct sieve_instance *svinst, const char *location,
		    const char *name, enum sieve_error *error_code_r)
{
	struct sieve_storage *storage;
	struct sieve_script *script;
	enum sieve_error error_code;

	if (error_code_r != NULL)
		*error_code_r = SIEVE_ERROR_NONE;
	else
		error_code_r = &error_code;

	storage = sieve_storage_create(svinst, location, 0, error_code_r);
	if (storage == NULL)
		return NULL;

	script = sieve_storage_get_script(storage, name, error_code_r);

	sieve_storage_unref(&storage);
	return script;
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
	enum sieve_error error_code;
	int ret;

	if (error_code_r != NULL)
		*error_code_r = SIEVE_ERROR_NONE;
	else
		error_code_r = &error_code;

	if (script->open)
		return 0;

	ret = script->v.open(script, error_code_r);
	i_assert(ret <= 0);
	if (ret < 0)
		return -1;

	i_assert(script->location != NULL);
	i_assert(script->name != NULL);
	script->open = TRUE;

	if (*script->name != '\0') {
		e_debug(script->event, "Opened script '%s' from '%s'",
			script->name, script->location);
	} else {
		e_debug(script->event, "Opened nameless script from '%s'",
			script->location);
	}
	return 0;
}

int sieve_script_open_as(struct sieve_script *script, const char *name,
			 enum sieve_error *error_code_r)
{
	if (sieve_script_open(script, error_code_r) < 0)
		return -1;

	/* override name */
	script->name = p_strdup(script->pool, name);
	event_add_str(script->event, "script_name", name);
	return 0;
}

struct sieve_script *
sieve_script_create_open(struct sieve_instance *svinst, const char *location,
			 const char *name, enum sieve_error *error_code_r)
{
	struct sieve_script *script;

	script = sieve_script_create(svinst, location, name, error_code_r);
	if (script == NULL)
		return NULL;

	if (sieve_script_open(script, error_code_r) < 0) {
		sieve_script_unref(&script);
		return NULL;
	}

	return script;
}

int sieve_script_check(struct sieve_instance *svinst, const char *location,
		       const char *name, enum sieve_error *error_code_r)
{
	struct sieve_script *script;
	enum sieve_error error_code;

	if (error_code_r == NULL)
		error_code_r = &error_code;

	script = sieve_script_create_open(svinst, location, name, error_code_r);
	if (script == NULL)
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

const char *sieve_script_location(const struct sieve_script *script)
{
	return script->location;
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
	enum sieve_error error_code;
	int ret;

	if (error_code_r != NULL)
		*error_code_r = SIEVE_ERROR_NONE;
	else
		error_code_r = &error_code;

	if (script->stream != NULL) {
		*stream_r = script->stream;
		return 0;
	}

	// FIXME: necessary?
	i_assert(script->open);

	T_BEGIN {
		ret = script->v.get_stream(script, &script->stream,
					   error_code_r);
	} T_END;

	if (ret < 0) {
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

bool sieve_script_equals(const struct sieve_script *script,
			 const struct sieve_script *other)
{
	if (script == other)
		return TRUE;
	if (script == NULL || other == NULL)
		return FALSE;
	if (script->script_class != other->script_class)
		return FALSE;

	if (script->v.equals == NULL) {
		i_assert (script->location != NULL && other->location != NULL);

		return (strcmp(script->location, other->location) == 0);
	}

	return script->v.equals(script, other);
}

unsigned int sieve_script_hash(const struct sieve_script *script)
{
	i_assert(script->name != NULL);

	return str_hash(script->name);
}

/*
 * Binary
 */

int sieve_script_binary_read_metadata(struct sieve_script *script,
				      struct sieve_binary_block *sblock,
				      sieve_size_t *offset)
{
	struct sieve_binary *sbin = sieve_binary_block_get_binary(sblock);
	string_t *storage_class, *location;
	unsigned int version;

	if ((sieve_binary_block_get_size(sblock) - *offset) == 0)
		return 0;

	/* storage class */
	if (!sieve_binary_read_string(sblock, offset, &storage_class)) {
		e_error(script->event,
			"Binary '%s' has invalid metadata for script '%s': "
			"Invalid storage class",
			sieve_binary_path(sbin), script->location);
		return -1;
	}
	if (strcmp(str_c(storage_class), script->driver_name) != 0) {
		e_debug(script->event,
			"Binary '%s' reports unexpected driver name for script '%s' "
			"('%s' rather than '%s')",
			sieve_binary_path(sbin), script->location,
			str_c(storage_class), script->driver_name);
		return 0;
	}

	/* version */
	if (!sieve_binary_read_unsigned(sblock, offset, &version)) {
		e_error(script->event,
			"Binary '%s' has invalid metadata for script '%s': "
			"Invalid version",
			sieve_binary_path(sbin), script->location);
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

	/* location */
	if (!sieve_binary_read_string(sblock, offset, &location)) {
		e_error(script->event,
			"Binary '%s' has invalid metadata for script '%s': "
			"Invalid location",
			sieve_binary_path(sbin), script->location);
		return -1;
	}
	i_assert(script->location != NULL);
	if (strcmp(str_c(location), script->location) != 0) {
		e_debug(script->event,
			"Binary '%s' reports different location "
			"for script '%s' (binary points to '%s')",
			sieve_binary_path(sbin), script->location,
			str_c(location));
		return 0;
	}

	if (script->v.binary_read_metadata == NULL)
		return 1;

	return script->v.binary_read_metadata(script, sblock, offset);
}

void sieve_script_binary_write_metadata(struct sieve_script *script,
					struct sieve_binary_block *sblock)
{
	sieve_binary_emit_cstring(sblock, script->driver_name);
	sieve_binary_emit_unsigned(sblock, script->storage->version);
	sieve_binary_emit_cstring(sblock, (script->location == NULL ?
					   "" : script->location));

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
	string_t *storage_class, *location;
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

	/* location */
	if (!sieve_binary_read_string(sblock, offset, &location))
		return FALSE;
	sieve_binary_dumpf(denv, "location = %s\n", str_c(location));

	if (script == NULL) {
		script = adhoc_script =
			sieve_script_create(svinst, str_c(location),
					    NULL, NULL);
	}

	if (script != NULL && script->v.binary_dump_metadata != NULL) {
		result = script->v.binary_dump_metadata(
			script, denv, sblock, offset);
	}

	sieve_script_unref(&adhoc_script);
	return result;
}

struct sieve_binary *
sieve_script_binary_load(struct sieve_script *script,
			 enum sieve_error *error_code_r)
{
	if (script->v.binary_load == NULL) {
		*error_code_r = SIEVE_ERROR_NOT_POSSIBLE;
		return NULL;
	}

	return script->v.binary_load(script, error_code_r);
}

int sieve_script_binary_save(struct sieve_script *script,
			     struct sieve_binary *sbin, bool update,
			     enum sieve_error *error_code_r)
{
	struct sieve_script *bin_script = sieve_binary_script(sbin);
	enum sieve_error error_code;

	if (error_code_r != NULL)
		*error_code_r = SIEVE_ERROR_NONE;
	else
		error_code_r = &error_code;

	i_assert(bin_script == NULL || sieve_script_equals(bin_script, script));

	if (script->v.binary_save == NULL) {
		*error_code_r = SIEVE_ERROR_NOT_POSSIBLE;
		return -1;
	}

	return script->v.binary_save(script, sbin, update, error_code_r);
}

const char *sieve_script_binary_get_prefix(struct sieve_script *script)
{
	struct sieve_storage *storage = script->storage;

	if (storage->bin_dir != NULL &&
	    sieve_storage_setup_bindir(storage, 0700) >= 0)
		return t_strconcat(storage->bin_dir, "/", script->name, NULL);
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
		sieve_storage_copy_error(storage->default_for, storage);
		return ret;
	}

	ret = sieve_storage_save_as(storage->default_for, input, newname);
	if (ret < 0) {
		sieve_storage_copy_error(storage, storage->default_for);
	} else if (sieve_script_is_active(script) > 0) {
		struct sieve_script *newscript;
		enum sieve_error error_code;

		newscript = sieve_storage_open_script(storage->default_for,
						      newname, &error_code);
		if (newscript == NULL) {
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
			sieve_storage_copy_error(storage->default_for, storage);
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

	/* Check script name */
	if (!sieve_script_name_is_valid(newname)) {
		sieve_script_set_error(script,
			SIEVE_ERROR_BAD_PARAMS,
			"Invalid new Sieve script name '%s'.",
			str_sanitize(newname, 80));
		return -1;
	}

	i_assert(script->open); // FIXME: auto-open?

	if (storage->default_for == NULL) {
		i_assert((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0);

		/* rename script */
		i_assert(script->v.rename != NULL);
		ret = script->v.rename(script, newname);

		/* rename INBOX mailbox attribute */
		if (ret >= 0 && oldname != NULL) {
			(void)sieve_storage_sync_script_rename(storage, oldname,
							       newname);
		}
	} else if (sieve_storage_check_script(storage->default_for,
					      newname, NULL) > 0) {
		sieve_script_set_error(script, SIEVE_ERROR_EXISTS,
			"A sieve script with that name already exists.");
		sieve_storage_copy_error(storage->default_for, storage);
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
	} else {
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

	/* Is the requested script active? */
	if (sieve_script_is_active(script) > 0) {
		is_active = TRUE;
		if (!ignore_active) {
			sieve_script_set_error(script, SIEVE_ERROR_ACTIVE,
				"Cannot delete the active Sieve script.");
			if (storage->default_for != NULL) {
				sieve_storage_copy_error(storage->default_for,
							 storage);
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

	/* Special handling if this is a default script */
	if (storage->default_for != NULL) {
		int ret = sieve_storage_active_script_is_default(
			storage->default_for);
		if (ret < 0)
			sieve_storage_copy_error(storage, storage->default_for);
		return ret;
	}

	if (script->v.is_active == NULL)
		return 0;
	return script->v.is_active(script);
}

int sieve_script_activate(struct sieve_script *script, time_t mtime)
{
	struct sieve_storage *storage = script->storage;
	int ret = 0;

	i_assert(script->open); // FIXME: auto-open?

	if (storage->default_for == NULL) {
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
		ret = sieve_storage_deactivate(storage->default_for,
					       (time_t)-1);
		if (ret < 0)
			sieve_storage_copy_error(storage, storage->default_for);
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

void sieve_script_sequence_init(struct sieve_script_sequence *sseq,
				struct sieve_storage *storage)
{
	sseq->storage = storage;
	sieve_storage_ref(storage);
}

struct sieve_script_sequence *
sieve_script_sequence_create(struct sieve_instance *svinst,
			     const char *location,
			     enum sieve_error *error_code_r)
{
	struct sieve_storage *storage;
	struct sieve_script_sequence *sseq;
	enum sieve_error error_code;

	if (error_code_r != NULL)
		*error_code_r = SIEVE_ERROR_NONE;
	else
		error_code_r = &error_code;

	storage = sieve_storage_create(svinst, location, 0, error_code_r);
	if (storage == NULL)
		return NULL;

	sseq = sieve_storage_get_script_sequence(storage, error_code_r);

	sieve_storage_unref(&storage);
	return sseq;
}

struct sieve_script *
sieve_script_sequence_next(struct sieve_script_sequence *sseq,
			   enum sieve_error *error_code_r)
{
	struct sieve_storage *storage = sseq->storage;

	i_assert(storage->v.script_sequence_next != NULL);
	return storage->v.script_sequence_next(sseq, error_code_r);
}

void sieve_script_sequence_free(struct sieve_script_sequence **_sseq)
{
	struct sieve_script_sequence *sseq = *_sseq;

	if (sseq == NULL)
		return;
	*_sseq = NULL;

	struct sieve_storage *storage = sseq->storage;

	if (storage->v.script_sequence_destroy != NULL)
		storage->v.script_sequence_destroy(sseq);

	sieve_storage_unref(&storage);
}
