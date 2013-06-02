/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "compat.h"
#include "unichar.h"
#include "str.h"
#include "hash.h"
#include "array.h"
#include "home-expand.h"
#include "mkdir-parents.h"
#include "eacces-error.h"
#include "istream.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-settings.h"
#include "sieve-error.h"
#include "sieve-binary.h"

#include "sieve-script-private.h"
#include "sieve-script-file.h"

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
	if ( namelen == 0 )
		return FALSE;

	/* Check worst-case maximum length */
	if ( namelen > SIEVE_MAX_SCRIPT_NAME_LEN * 4 )
		return FALSE;

	/* Intialize array for unicode characters */
	t_array_init(&uni_name, namelen * 4);

	/* Convert UTF-8 to UCS4/UTF-32 */
	if ( uni_utf8_to_ucs4(scriptname, &uni_name) < 0 )
		return FALSE;
	name_chars = array_get(&uni_name, &count);

	/* Check true maximum length */
	if ( count > SIEVE_MAX_SCRIPT_NAME_LEN )
		return FALSE;

	/* Scan name for invalid characters
	 *   FIXME: compliance with Net-Unicode Definition (Section 2 of
	 *          RFC 5198) is not checked fully and no normalization
	 *          is performed.
	 */
	for ( i = 0; i < count; i++ ) {

		/* 0000-001F; [CONTROL CHARACTERS] */
		if ( name_chars[i] <= 0x001f )
			return FALSE;

		/* 002F; SLASH (not RFC-prohibited, but '/' is dangerous) */
		if ( name_chars[i] == 0x002f )
			return FALSE;

		/* 007F; DELETE */
		if ( name_chars[i] == 0x007f )
			return FALSE;

		/* 0080-009F; [CONTROL CHARACTERS] */
		if ( name_chars[i] >= 0x0080 && name_chars[i] <= 0x009f )
			return FALSE;

		/* 00FF */
		if ( name_chars[i] == 0x00ff )
			return FALSE;

		/* 2028; LINE SEPARATOR */
		/* 2029; PARAGRAPH SEPARATOR */
		if ( name_chars[i] == 0x2028 || name_chars[i] == 0x2029 )
			return FALSE;
	}

	return TRUE;
}

/*
 * Script object
 */

static const char *split_next_arg(const char *const **_args)
{
	const char *const *args = *_args;
	const char *str = args[0];

	/* join arguments for escaped ";" separator */

	args++;
	while (*args != NULL && **args == '\0') {
		args++;
		if (*args == NULL) {
			/* string ends with ";", just ignore it. */
			break;
		}
		str = t_strconcat(str, ";", *args, NULL);
		args++;
	}
	*_args = args;
	return str;
}

static bool sieve_script_location_parse
(struct sieve_script *script, const char *data, const char **location_r,
	const char *const **options_r, const char **error_r)
{
	ARRAY_TYPE(const_string) options;
	const char *const *tmp;

	if (*data == '\0') {
		*options_r = NULL;
		*location_r = data;
		return TRUE;
	}

	/* <location> */
	tmp = t_strsplit(data, ";");
	*location_r = split_next_arg(&tmp);

	if ( options_r != NULL ) {
		t_array_init(&options, 8);

		/* [<option> *(';' <option>)] */
		while (*tmp != NULL) {
			const char *option = split_next_arg(&tmp);

			if ( strncasecmp(option, "name=", 5) == 0 ) {
				if ( option[5] == '\0' ) {
					*error_r = "empty name not allowed";
					return FALSE;
				}

				if ( script->name == NULL )
					script->name = p_strdup(script->pool, option+5);

			} else if ( strncasecmp(option, "bindir=", 7) == 0 ) {
				const char *bin_dir = option+7;

				if ( bin_dir[0] == '\0' ) {
					*error_r = "empty bindir not allowed";
					return FALSE;
				}

				if ( bin_dir[0] == '~' ) {
					/* home-relative path. change to absolute. */
					const char *home = sieve_environment_get_homedir(script->svinst);

					if ( home != NULL ) {
						bin_dir = home_expand_tilde(bin_dir, home);
					} else if ( bin_dir[1] == '/' || bin_dir[1] == '\0' ) {
						*error_r = "bindir is relative to home directory (~/), "
							"but home directory cannot be determined";
						return FALSE;
					}
				}

				script->bin_dir = p_strdup(script->pool, bin_dir);
			} else {
				array_append(&options, &option, 1);
			}
		}

		(void)array_append_space(&options);
		*options_r = array_idx(&options, 0);
	}

	return TRUE;
}

void sieve_script_init
(struct sieve_script *script, struct sieve_instance *svinst,
	const struct sieve_script *script_class, const char *data,
	const char *name, struct sieve_error_handler *ehandler)
{
	script->script_class = script_class;
	script->refcount = 1;
	script->svinst = svinst;
	script->ehandler = ehandler;
	script->data = p_strdup_empty(script->pool, data);
	script->name = p_strdup_empty(script->pool, name);

	sieve_error_handler_ref(ehandler);
}

struct sieve_script *sieve_script_create
(struct sieve_instance *svinst, const char *location, const char *name,
	struct sieve_error_handler *ehandler, enum sieve_error *error_r)
{
	struct sieve_script *script;
	const struct sieve_script *script_class;
	const char *data, *p;

	p = strchr(location, ':');
	if ( p == NULL ) {
		/* Default script driver is "file" (meaning that location is a fs path) */
		data = location;
		script_class = &sieve_file_script;
	} else {
		/* Lookup script driver */
		T_BEGIN {
			const char *driver;

			data = p+1;
			driver = t_strdup_until(location, p);

			/* FIXME
			script_class = sieve_script_class_lookup(driver);*/
			if ( strcasecmp(driver, SIEVE_FILE_SCRIPT_DRIVER_NAME) == 0 )
				script_class = &sieve_file_script;
			else if ( strcasecmp(driver, SIEVE_DICT_SCRIPT_DRIVER_NAME) == 0 )
				script_class = &sieve_dict_script;
			else
				script_class = NULL;

			if ( script_class == NULL ) {
				sieve_sys_error(svinst,
					"Unknown sieve script driver module: %s", driver);
			}
		} T_END;
	}

	if ( script_class == NULL ) {
		if ( error_r != NULL )
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
		return NULL;
	}

	script = script_class->v.alloc();
	sieve_script_init(script, svinst, script_class, data, name, ehandler);
	script->location = p_strdup(script->pool, location);
	return script;
}

int sieve_script_open
(struct sieve_script *script, enum sieve_error *error_r)
{
	struct sieve_instance *svinst = script->svinst;
	struct sieve_error_handler *ehandler = script->ehandler;
	enum sieve_error error;
	const char *const *options = NULL;
	const char *location = NULL, *parse_error = NULL;

	if ( error_r != NULL )
		*error_r = SIEVE_ERROR_NONE;

	if ( script->open )
		return 0;

	if ( !sieve_script_location_parse
		(script, script->data, &location, &options, &parse_error) ) {
		sieve_critical(svinst, ehandler, NULL,
			"failed to access sieve script", "failed to parse script location: %s",
			parse_error);
		if ( error_r != NULL )
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}

	script->location = NULL;
	if ( script->v.open(script, location, options, &error) < 0 ) {
		if ( error_r == NULL ) {
			if ( error == SIEVE_ERROR_NOT_FOUND )
				sieve_error(ehandler, script->name, "sieve script does not exist");
		} else {
			*error_r = error;
		}
		return -1;
	}

	i_assert( script->location != NULL );
	i_assert( script->name != NULL );
	script->open = TRUE;
	return 0;
}

int sieve_script_open_as
(struct sieve_script *script, const char *name, enum sieve_error *error_r)
{
	if ( sieve_script_open(script, error_r) < 0 )
		return -1;

	/* override name */
	script->name = p_strdup(script->pool, name);
	return 0;
}

struct sieve_script *sieve_script_create_open
(struct sieve_instance *svinst, const char *location, const char *name,
	struct sieve_error_handler *ehandler, enum sieve_error *error_r)
{
	struct sieve_script *script;

	script = sieve_script_create(svinst, location, name, ehandler, error_r);
	if ( script == NULL )
		return NULL;

	if ( sieve_script_open(script, error_r) < 0 ) {
		sieve_script_unref(&script);
		return NULL;
	}
	
	return script;
}

struct sieve_script *sieve_script_create_open_as
(struct sieve_instance *svinst, const char *location, const char *name,
	struct sieve_error_handler *ehandler, enum sieve_error *error_r)
{
	struct sieve_script *script;

	script = sieve_script_create(svinst, location, name, ehandler, error_r);
	if ( script == NULL )
		return NULL;

	if ( sieve_script_open_as(script, name, error_r) < 0 ) {
		sieve_script_unref(&script);
		return NULL;
	}
	
	return script;
}

void sieve_script_ref(struct sieve_script *script)
{
	script->refcount++;
}

void sieve_script_unref(struct sieve_script **_script)
{
	struct sieve_script *script = *_script;

	i_assert(script->refcount > 0);

	if (--script->refcount != 0)
		return;

	if ( script->stream != NULL )
		i_stream_unref(&script->stream);

	if ( script->ehandler != NULL )
		sieve_error_handler_unref(&script->ehandler);

	if ( script->v.destroy != NULL )
		script->v.destroy(script);

	pool_unref(&script->pool);
	*_script = NULL;
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
	return script->svinst;
}

int sieve_script_get_size(struct sieve_script *script, uoff_t *size_r)
{
	struct istream *stream;
	int ret;

	if ( script->v.get_size != NULL ) {
		if ( (ret=script->v.get_size(script, size_r)) != 0)
			return ret;
	}

	/* Try getting size from the stream */
	if ( script->stream == NULL &&
		sieve_script_get_stream(script, &stream, NULL) < 0 )
		return -1;

	return i_stream_get_size(script->stream, TRUE, size_r);
}

bool sieve_script_is_open(const struct sieve_script *script)
{
	return script->open;
}

/*
 * Stream management
 */

int sieve_script_get_stream
(struct sieve_script *script, struct istream **stream_r,
	enum sieve_error *error_r)
{
	enum sieve_error error;
	int ret;

	if ( error_r != NULL )
		*error_r = SIEVE_ERROR_NONE;

	if ( script->stream != NULL ) {
		*stream_r = script->stream;
		return 0;
	}

	T_BEGIN {
		ret = script->v.get_stream(script, &script->stream, &error);
	} T_END;

	if ( ret < 0 ) {
		if ( error_r == NULL ) {
			if ( error == SIEVE_ERROR_NOT_FOUND ) {
				sieve_error(script->ehandler, script->name,
					"sieve script does not exist");
			}
		} else {
			*error_r = error;
		}
		return -1;
	}

	*stream_r = script->stream;
	return 0;
}

/*
 * Comparison
 */

bool sieve_script_equals
(const struct sieve_script *script, const struct sieve_script *other)
{
	if ( script == other )
		return TRUE;

	if ( script == NULL || other == NULL )
		return FALSE;

	if ( script->script_class != other->script_class )
		return FALSE;

	if ( script->v.equals == NULL ) {
		i_assert ( script->location != NULL && other->location != NULL);

		return ( strcmp(script->location, other->location) == 0 );
	}

	return script->v.equals(script, other);
}

unsigned int sieve_script_hash(const struct sieve_script *script)
{
	i_assert( script->name != NULL );

	return str_hash(script->name);
}

/*
 * Binary
 */

int sieve_script_binary_read_metadata
(struct sieve_script *script, struct sieve_binary_block *sblock,
	sieve_size_t *offset)
{
	struct sieve_instance *svinst = script->svinst;
	struct sieve_binary *sbin = sieve_binary_block_get_binary(sblock);
	string_t *script_class;

	if ( sieve_binary_block_get_size(sblock) - *offset == 0 )
		return 0;

	if ( !sieve_binary_read_string(sblock, offset, &script_class) ) {
		sieve_sys_error(svinst,
			"sieve script: binary %s has invalid metadata for script %s",
			sieve_binary_path(sbin), sieve_script_location(script));
		return -1;
	}

	if ( strcmp(str_c(script_class), script->driver_name) != 0 )
		return 0;

	if ( script->v.binary_read_metadata == NULL )
		return 1;

	return script->v.binary_read_metadata(script, sblock, offset);
}

void sieve_script_binary_write_metadata
(struct sieve_script *script, struct sieve_binary_block *sblock)
{
	sieve_binary_emit_cstring(sblock, script->driver_name);

	if ( script->v.binary_write_metadata == NULL )
		return;

	script->v.binary_write_metadata(script, sblock);
}

struct sieve_binary *sieve_script_binary_load
(struct sieve_script *script, enum sieve_error *error_r)
{
	if ( script->v.binary_load == NULL ) {
		*error_r = SIEVE_ERROR_NOT_POSSIBLE;
		return NULL;
	}

	return script->v.binary_load(script, error_r);
}

int sieve_script_binary_save
(struct sieve_script *script, struct sieve_binary *sbin, bool update,
	enum sieve_error *error_r)
{
	struct sieve_script *bin_script = sieve_binary_script(sbin);

	i_assert(bin_script == NULL || sieve_script_equals(bin_script, script));

	if ( script->v.binary_save == NULL ) {
		*error_r = SIEVE_ERROR_NOT_POSSIBLE;
		return -1;
	}

	return script->v.binary_save(script, sbin, update, error_r);
}

int sieve_script_setup_bindir
(struct sieve_script *script, mode_t mode)
{
	struct sieve_instance *svinst = script->svinst;
	struct stat st;

	if ( script->bin_dir == NULL )
		return -1;

	if ( stat(script->bin_dir, &st) == 0 )
		return 0;

	if ( errno == EACCES ) {
		sieve_sys_error(svinst, "sieve script: "
			"failed to setup directory for binaries: %s",
			eacces_error_get("stat", script->bin_dir));
		return -1;
	} else if ( errno != ENOENT ) {
		sieve_sys_error(svinst, "sieve script: "
			"failed to setup directory for binaries: stat(%s) failed: %m",
			script->bin_dir);
		return -1;
	}

	if ( mkdir_parents(script->bin_dir, mode) == 0 ) {
		if ( svinst->debug )
			sieve_sys_debug(svinst, "sieve script: "
				"created directory for binaries: %s", script->bin_dir);
		return 1;
	}

	switch ( errno ) {
	case EEXIST:
		return 0;
	case ENOENT:
		sieve_sys_error(svinst, "sieve script: "
			"directory for binaries was deleted while it was being created");
		break;
	case EACCES:
		sieve_sys_error(svinst, "sieve script: %s",
			eacces_error_get_creating("mkdir_parents_chgrp", script->bin_dir));
		break;
	default:
		sieve_sys_error(svinst, "sieve script: "
			"mkdir_parents_chgrp(%s) failed: %m", script->bin_dir);
		break;
	}

	return -1;
}

