/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "str.h"
#include "str-sanitize.h"
#include "hash.h"
#include "home-expand.h"
#include "eacces-error.h"
#include "mkdir-parents.h"
#include "ioloop.h"
#include "settings.h"

#include "sieve-common.h"
#include "sieve-settings.old.h"
#include "sieve-error-private.h"

#include "sieve-script-private.h"
#include "sieve-storage-private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>

struct event_category event_category_sieve_storage = {
	.parent = &event_category_sieve,
	.name = "sieve-storage",
};

/*
 * Storage name
 */

bool sieve_storage_name_is_valid(const char *name)
{
	return sieve_script_name_is_valid(name);
}

/*
 * Storage class
 */

struct sieve_storage_class_registry {
	ARRAY_TYPE(sieve_storage_class) storage_classes;
};

void sieve_storages_init(struct sieve_instance *svinst)
{
	svinst->storage_reg = p_new(svinst->pool,
				    struct sieve_storage_class_registry, 1);
	p_array_init(&svinst->storage_reg->storage_classes, svinst->pool, 8);

	sieve_storage_class_register(svinst, &sieve_file_storage);
	sieve_storage_class_register(svinst, &sieve_dict_storage);
	sieve_storage_class_register(svinst, &sieve_ldap_storage);
}

void sieve_storages_deinit(struct sieve_instance *svinst ATTR_UNUSED)
{
	/* nothing yet */
}

void sieve_storage_class_register(struct sieve_instance *svinst,
				  const struct sieve_storage *storage_class)
{
	struct sieve_storage_class_registry *reg = svinst->storage_reg;
	const struct sieve_storage *old_class;

	old_class = sieve_storage_class_find(svinst,
					     storage_class->driver_name);
	if (old_class != NULL) {
		if (old_class->v.alloc == NULL) {
			/* replacing a "support not compiled in" storage class
			 */
			sieve_storage_class_unregister(svinst, old_class);
		} else {
			i_panic("sieve_storage_class_register(%s): "
				"Already registered",
				storage_class->driver_name);
		}
	}

	array_append(&reg->storage_classes, &storage_class, 1);
}

void sieve_storage_class_unregister(struct sieve_instance *svinst,
				    const struct sieve_storage *storage_class)
{
	struct sieve_storage_class_registry *reg = svinst->storage_reg;
	const struct sieve_storage *const *classes;
	unsigned int i, count;

	classes = array_get(&reg->storage_classes, &count);
	for (i = 0; i < count; i++) {
		if (classes[i] == storage_class) {
			array_delete(&reg->storage_classes, i, 1);
			break;
		}
	}
}

const struct sieve_storage *
sieve_storage_class_find(struct sieve_instance *svinst, const char *name)
{
	struct sieve_storage_class_registry *reg = svinst->storage_reg;
	const struct sieve_storage *const *classes;
	unsigned int i, count;

	i_assert(name != NULL);

	classes = array_get(&reg->storage_classes, &count);
	for (i = 0; i < count; i++) {
		if (strcasecmp(classes[i]->driver_name, name) == 0)
			return classes[i];
	}
	return NULL;
}

bool sieve_storage_class_exists(struct sieve_instance *svinst,
				const char *name)
{
	return (sieve_storage_class_find(svinst, name) != NULL);
}

/*
 * Storage event
 */

static void
sieve_storage_update_event_prefix(struct event *event, const char *storage_name,
				  bool is_default)
{
	string_t *prefix = t_str_new(128);

	str_append(prefix, "storage");
	if (storage_name != NULL && *storage_name != '\0') {
		str_append_c(prefix, ' ');
		str_append(prefix, storage_name);
	}
	if (is_default)
		str_append(prefix, " (default)");
	str_append(prefix, ": ");
	event_set_append_log_prefix(event, str_c(prefix));
}

static struct event *
sieve_storage_create_event(struct sieve_instance *svinst,
			   struct event *event_parent,
			   const char *storage_name)
{
	struct event *event;

	event = event_create(event_parent == NULL ?
			     svinst->event : event_parent);
	if (event_parent != svinst->event)
		event_add_category(event, &event_category_sieve);
	event_add_category(event, &event_category_sieve_storage);

	sieve_storage_update_event_prefix(event, storage_name, FALSE);
	return event;
}

static struct event *
sieve_storage_create_driver_event(struct event *event_parent,
				  const char *driver_name)
{
	struct event *event;

	event = event_create(event_parent);
	event_add_str(event, "driver", driver_name);
	event_set_append_log_prefix(event,
		t_strdup_printf("%s: ", driver_name));

	return event;
}

/*
 * Storage instance
 */

static int
sieve_storage_alloc_from_class(struct sieve_instance *svinst,
			       struct event *event,
			       const struct sieve_storage *storage_class,
			       const char *cause, const char *script_type,
			       const char *storage_name,
			       const char *script_name,
			       enum sieve_storage_flags flags,
			       struct sieve_storage **storage_r,
			       enum sieve_error *error_code_r,
			       const char **error_r)
{
	struct sieve_storage *storage;

	i_assert(svinst->username != NULL);

	if (storage_class->v.alloc == NULL) {
		e_error(event, "Support not compiled in for this driver");
		sieve_error_create_script_not_found(
			script_name, error_code_r, error_r);
		return -1;
	}

	if ((flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) != 0 &&
	    !storage_class->allows_synchronization) {
		e_error(event, "Storage does not support synchronization");
		sieve_error_create_internal(error_code_r, error_r);
		return -1;
	}
	if ((flags & SIEVE_STORAGE_FLAG_READWRITE) != 0 &&
	    storage_class->v.save_init == NULL) {
		e_error(event, "Storage does not support write access");
		sieve_error_create_internal(error_code_r, error_r);
		return -1;
	}

	storage = storage_class->v.alloc();
	storage->storage_class = storage_class;
	storage->refcount = 1;
	storage->svinst = svinst;
	storage->cause = p_strdup(storage->pool, cause);
	storage->type = p_strdup(storage->pool, script_type);
	storage->script_name = p_strdup(storage->pool, script_name);
	storage->flags = flags;

	if (storage_name != NULL && *storage_name != '\0')
		storage->name = p_strdup(storage->pool, storage_name);
	else {
		storage->name = p_strconcat(
			storage->pool, "auto:", storage->type, NULL);
	}

	storage->event = event;
	event_ref(event);

	*storage_r = storage;
	return 0;
}

int sieve_storage_alloc(struct sieve_instance *svinst,
			struct event *event_parent,
			const struct sieve_storage *storage_class,
			const char *cause, const char *script_type,
			const char *storage_name, const char *script_name,
			enum sieve_storage_flags flags,
			struct sieve_storage **storage_r,
			enum sieve_error *error_code_r, const char **error_r)
{
	struct event *storage_event, *event;
	int ret;

	*storage_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	storage_event = sieve_storage_create_event(svinst, event_parent,
						   storage_name);
	event = sieve_storage_create_driver_event(storage_event,
						  storage_class->driver_name);
	event_unref(&storage_event);

	ret = sieve_storage_alloc_from_class(svinst, event, storage_class,
					     cause, script_type,
					     storage_name, script_name, flags,
					     storage_r, error_code_r, error_r);

	event_unref(&event);

	return ret;
}

int sieve_storage_alloc_with_settings(struct sieve_instance *svinst,
				      struct event *event_parent,
				      const struct sieve_storage *storage_class,
				      const char *cause,
				      const struct sieve_storage_settings *set,
				      enum sieve_storage_flags flags,
				      struct sieve_storage **storage_r,
				      enum sieve_error *error_code_r,
				      const char **error_r)
{
	struct sieve_storage *storage;
	int ret;

	*storage_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	ret = sieve_storage_alloc_from_class(svinst, event_parent,
					     storage_class,
					     cause, set->script_type,
					     set->script_storage,
					     set->script_name, flags,
					     &storage, error_code_r, error_r);
	if (ret < 0)
		return -1;

	storage->bin_path = p_strdup_empty(storage->pool, set->script_bin_path);
	storage->max_storage = set->quota_storage_size;
	storage->max_scripts = set->quota_script_count;

	if (storage->max_storage > 0) {
		e_debug(storage->event, "quota: "
			"Storage limit: %"PRIuUOFF_T" bytes",
			storage->max_storage);
	}
	if (storage->max_scripts > 0) {
		e_debug(storage->event, "quota: "
			"Script count limit: %u scripts",
			storage->max_scripts);
	}

	*storage_r = storage;
	return 0;
}

static int
sieve_storage_alloc_from_settings(struct sieve_instance *svinst,
				  struct event *event_parent, const char *cause,
				  const struct sieve_storage_settings *set,
				  enum sieve_storage_flags flags,
				  struct sieve_storage **storage_r,
				  enum sieve_error *error_code_r,
				  const char **error_r)
{
	const struct sieve_storage *storage_class;
	struct event *event = event_parent;
	int ret;

	*storage_r = NULL;

	if (!sieve_storage_settings_match_script_cause(set, cause))
		return 0;

	storage_class = sieve_storage_class_find(svinst, set->script_driver);
	// FIXME: add support for automatic module loading (no such modules yet)
	if (storage_class == NULL) {
		e_error(event, "Unknown storage driver: %s",
			set->script_driver);
		sieve_error_create_script_not_found(set->script_name,
						    error_code_r, error_r);
		event_unref(&event);
		return -1;
	}

	event = sieve_storage_create_driver_event(event_parent,
						  storage_class->driver_name);

	ret = sieve_storage_alloc_with_settings(svinst, event, storage_class,
						cause, set, flags,
						storage_r, error_code_r,
						error_r);

	event_unref(&event);

	if (ret < 0)
		return -1;
	return 1;
}

static int
sieve_storage_autodetect(struct sieve_instance *svinst, struct event *event,
			 const char *cause, const char *type,
			 const struct sieve_storage_settings *set,
			 enum sieve_storage_flags flags,
			 struct sieve_storage **storage_r,
			 enum sieve_error *error_code_r, const char **error_r)
{
	struct sieve_storage_class_registry *reg = svinst->storage_reg;
	int ret;

	*storage_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	if (!sieve_storage_settings_match_script_cause(set, cause))
		return 0;
	if (!sieve_storage_settings_match_script_type(set, type))
		return 0;

	const struct sieve_storage *const *classes;
	unsigned int i, count;

	classes = array_get(&reg->storage_classes, &count);
	ret = 0;
	for (i = 0; i < count; i++) {
		if (classes[i]->v.autodetect == NULL)
			continue;
		if (set->script_driver[0] != '\0' &&
		    strcasecmp(set->script_driver,
			       classes[i]->driver_name) != 0)
			continue;

		struct event *driver_event =
			sieve_storage_create_driver_event(
				event, classes[i]->driver_name);

		*storage_r = NULL;
		ret = classes[i]->v.autodetect(svinst, driver_event,
					       cause, set, flags,
					       storage_r, error_code_r,
					       error_r);

		event_unref(&driver_event);

		if (ret < 0) {
			i_assert(*error_code_r != SIEVE_ERROR_NONE);
			i_assert(*error_r != NULL);
			if (*error_code_r == SIEVE_ERROR_NOT_FOUND) {
				*error_code_r = SIEVE_ERROR_NONE;
				*error_r = NULL;
				ret = 0;
			}
		}
		i_assert(ret <= 0 || *storage_r != NULL);
		if (ret != 0)
			break;
	}

	if (ret == 0)
		e_debug(event, "Autodetection failed");
	return ret;
}

static int
sieve_storage_autodetect_any(struct sieve_instance *svinst,
			     struct event *event_parent,
			     const char *cause, const char *type,
			     const struct sieve_storage_settings *set,
			     enum sieve_storage_flags flags,
			     struct sieve_storage **storage_r,
			     enum sieve_error *error_code_r,
			     const char **error_r)
{
	struct event *event;
	int ret;

	event = sieve_storage_create_event(svinst, event_parent, NULL);

	ret = sieve_storage_autodetect(svinst, event, cause, type, set,
				       flags, storage_r, error_code_r, error_r);

	event_unref(&event);
	return ret;
}

static int
sieve_storage_init_real(struct sieve_instance *svinst, struct event *event,
			const char *cause, const char *type,
			const char *storage_name, bool try,
			enum sieve_storage_flags flags,
			struct sieve_storage **storage_r,
			enum sieve_error *error_code_r, const char **error_r)
{
	const struct sieve_storage_settings *set;
	struct sieve_storage *storage;
	const char *error;
	int ret;

	if (try) {
		ret = settings_try_get_filter(
			event, "sieve_script", storage_name,
			&sieve_storage_setting_parser_info, 0,
			&set, &error);
		if (ret == 0)
			return 0;
	} else {
		ret = settings_get_filter(
			event, "sieve_script", storage_name,
			&sieve_storage_setting_parser_info, 0,
			&set, &error);
	}
	if (ret < 0) {
		e_error(event, "%s", error);
		sieve_error_create_internal(error_code_r, error_r);
		return -1;
	}

	if (!sieve_storage_settings_match_script_type(set, type)) {
		settings_free(set);
		return 0;
	}

	event_add_str(event, "sieve_script", storage_name);
	if (set->script_driver[0] == '\0') {
		ret = sieve_storage_autodetect(svinst, event, cause, type,
					       set, flags, storage_r,
					       error_code_r, error_r);
		if (ret != 0) {
			settings_free(set);
			return ret;
		}
		e_error(event, "sieve_script_driver is empty");
		sieve_error_create_script_not_found(set->script_name,
						    error_code_r, error_r);
		settings_free(set);
		return -1;
	}

	ret = sieve_storage_alloc_from_settings(svinst, event, cause,
						set, flags, &storage,
						error_code_r, error_r);
	settings_free(set);
	if (ret <= 0)
		return ret;
	i_assert(storage != NULL);
	i_assert(storage->v.init != NULL);

	T_BEGIN {
		ret = storage->v.init(storage);
		i_assert(ret <= 0);
	} T_END;
	if (ret < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
		*error_code_r = storage->error_code;
		*error_r = t_strdup(storage->error);
		sieve_storage_unref(&storage);
		return -1;
	}
	*storage_r = storage;
	return 1;
}

static int
sieve_storage_init(struct sieve_instance *svinst, struct event *event_parent,
		   const char *cause, const char *type,
		   const char *storage_name, bool try,
		   enum sieve_storage_flags flags,
		   struct sieve_storage **storage_r,
		   enum sieve_error *error_code_r, const char **error_r)
{
	struct event *event;
	int ret;

	*storage_r = NULL;

	event = sieve_storage_create_event(svinst, event_parent, storage_name);

	ret = sieve_storage_init_real(svinst, event, cause, type,
				      storage_name, try, flags,
				      storage_r, error_code_r, error_r);

	event_unref(&event);

	return ret;
}

int sieve_storage_create(struct sieve_instance *svinst,
			 struct event *event, const char *cause,
			 const char *storage_name,
			 enum sieve_storage_flags flags,
			 struct sieve_storage **storage_r,
			 enum sieve_error *error_code_r, const char **error_r)
{
	struct sieve_storage *storage;
	int ret;

	*storage_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	ret = sieve_storage_init(svinst, event, cause, SIEVE_STORAGE_TYPE_ANY,
				 storage_name, TRUE, flags,
				 &storage, error_code_r, error_r);
	if (ret < 0) {
		if (*error_code_r != SIEVE_ERROR_NOT_FOUND)
			return -1;
		ret = 0;
	}
	if (ret == 0) {
		e_debug(event, "Sieve script storage '%s' not found (cause=%s)",
			storage_name, cause);
		sieve_error_create_script_not_found(
			NULL, error_code_r, error_r);
		return -1;
	}
	i_assert(storage != NULL);
	*storage_r = storage;
	return 0;
}

int sieve_storage_create_auto(struct sieve_instance *svinst,
			      struct event *event,
			      const char *cause, const char *type,
			      enum sieve_storage_flags flags,
			      struct sieve_storage **storage_r,
			      enum sieve_error *error_code_r,
			      const char **error_r)
{
	const struct sieve_storage_settings *storage_set;
	const char *const *storage_names;
	unsigned int i, count;
	const char *error;

	*storage_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	if (settings_get(event, &sieve_storage_setting_parser_info,
			 SETTINGS_GET_FLAG_SORT_FILTER_ARRAYS,
			 &storage_set, &error) < 0) {
		e_error(event, "%s", error);
		sieve_error_create_internal(error_code_r, error_r);
		return -1;
	}
	if (array_is_created(&storage_set->storages))
		storage_names = array_get(&storage_set->storages, &count);
	else {
		storage_names = NULL;
		count = 0;
	}

	struct sieve_storage *storage = NULL;
	int ret = 0;

	for (i = 0; i < count; i++) {
		ret = sieve_storage_init(svinst, event, cause, type,
					 storage_names[i], FALSE, flags,
					 &storage, error_code_r, error_r);
		if (ret < 0 && *error_code_r != SIEVE_ERROR_NOT_FOUND) {
			settings_free(storage_set);
			return -1;
		}
		if (ret > 0) {
			i_assert(storage != NULL);
			break;
		}
	}
	if (ret <= 0) {
		ret = sieve_storage_autodetect_any(svinst, event, cause, type,
						   storage_set, flags, &storage,
						   error_code_r, error_r);
		if (ret < 0) {
			settings_free(storage_set);
			return -1;
		}
	}
	settings_free(storage_set);
	if (ret <= 0) {
		e_debug(event,
			"storage: No matching Sieve storage configured "
			"(type=%s and cause=%s)",
			type, cause);
		sieve_error_create_script_not_found(
			NULL, error_code_r, error_r);
		return -1;
	}
	i_assert(storage != NULL);
	*storage_r = storage;
	return 0;
}

static int
sieve_storage_create_default(struct sieve_instance *svinst,
			     struct event *event, const char *cause,
			     enum sieve_storage_flags flags,
			     struct sieve_storage **storage_r,
			     enum sieve_error *error_code_r,
			     const char **error_r)
{
	struct sieve_storage *storage;
	enum sieve_error error_code;
	int ret;

	*storage_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	ret = sieve_storage_create_auto(svinst, event, cause,
					SIEVE_STORAGE_TYPE_DEFAULT, flags,
					&storage, &error_code, error_r);
	if (ret >= 0) {
		storage->is_default = TRUE;
		sieve_storage_update_event_prefix(
			event_get_parent(storage->event), storage->name, TRUE);
	} else {
		switch (error_code) {
		case SIEVE_ERROR_NOT_FOUND:
			e_debug(event, "storage: "
				"Default script not found");
			break;
		case SIEVE_ERROR_TEMP_FAILURE:
			e_error(event, "storage: "
				"Failed to access default script "
				"(temporary failure)");
			break;
		default:
			e_error(event, "storage: "
				"Failed to access default script");
			break;
		}
		*error_code_r = error_code;
	}

	*storage_r = storage;
	return ret;
}

static int
sieve_storage_create_default_for(struct sieve_storage *storage,
				 struct sieve_storage **storage_r,
				 enum sieve_error *error_code_r,
				 const char **error_r)
{
	*storage_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	if (storage->default_storage != NULL) {
		sieve_storage_ref(storage->default_storage);
		*storage_r = storage->default_storage;
		return 0;
	}

	struct sieve_instance *svinst = storage->svinst;
	enum sieve_error error_code;
	const char *error;

	i_assert(storage->default_storage_for == NULL);
	if (sieve_storage_create_default(svinst, svinst->event,
					 storage->cause, 0,
					 &storage->default_storage,
					 &error_code, &error) < 0) {
		sieve_storage_set_error(storage, error_code, "%s", error);
		*error_code_r = storage->error_code;
		*error_r = storage->error;
		return -1;
	}

	storage->default_storage->default_storage_for = storage;
	sieve_storage_ref(storage);

	*storage_r = storage->default_storage;
	return 0;
}

int sieve_storage_create_personal(struct sieve_instance *svinst,
				  struct mail_user *user, const char *cause,
				  enum sieve_storage_flags flags,
				  struct sieve_storage **storage_r,
				  enum sieve_error *error_code_r)
{
	struct sieve_storage *storage;
	int ret;

	*storage_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);

	/* Check whether Sieve is disabled for this user */
	if (!svinst->set->enabled) {
		e_debug(svinst->event,
			"Sieve is disabled for this user");
		*error_code_r = SIEVE_ERROR_NOT_POSSIBLE;
		return -1;
	}

	/* Attempt to locate user's main storage */
	ret = sieve_storage_create_auto(svinst, svinst->event, cause,
					SIEVE_STORAGE_TYPE_PERSONAL, flags,
					&storage, error_code_r, NULL);
	if (ret == 0) {
		(void)sieve_storage_sync_init(storage, user);
	} else if (*error_code_r != SIEVE_ERROR_TEMP_FAILURE &&
		   (flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) == 0 &&
		   (flags & SIEVE_STORAGE_FLAG_READWRITE) == 0) {

		/* Failed; try using default script location
		   (not for temporary failures, read/write access, or dsync) */
		ret = sieve_storage_create_default(svinst, svinst->event,
						   cause, flags, &storage,
						   error_code_r, NULL);
	}
	*storage_r = storage;
	return ret;
}

void sieve_storage_ref(struct sieve_storage *storage)
{
	storage->refcount++;
}

void sieve_storage_unref(struct sieve_storage **_storage)
{
	struct sieve_storage *storage = *_storage;

	if (storage == NULL)
		return;
	*_storage = NULL;

	i_assert(storage->refcount > 0);
	if (--storage->refcount != 0)
		return;

	if (storage->default_storage_for != NULL) {
		i_assert(storage->is_default);
		storage->default_storage_for->default_storage = NULL;
		sieve_storage_unref(&storage->default_storage_for);
	}

	sieve_storage_sync_deinit(storage);

	if (storage->v.destroy != NULL)
		storage->v.destroy(storage);

	i_free(storage->error);
	event_unref(&storage->event);
	pool_unref(&storage->pool);
}

/*
 * Binary
 */

int sieve_storage_setup_bin_path(struct sieve_storage *storage, mode_t mode)
{
	const char *bin_path = storage->bin_path;
	struct stat st;

	if (bin_path == NULL) {
		sieve_storage_set_critical(
			storage, "script_bin_path not configured for storage");
		return -1;
	}

	if (stat(bin_path, &st) == 0)
		return 0;

	if (errno == EACCES) {
		sieve_storage_set_critical(storage,
			"Failed to setup directory for binaries: "
			"%s", eacces_error_get("stat", bin_path));
		return -1;
	} else if (errno != ENOENT) {
		sieve_storage_set_critical(storage,
			"Failed to setup directory for binaries: "
			"stat(%s) failed: %m",
			bin_path);
		return -1;
	}

	if (mkdir_parents(bin_path, mode) == 0) {
		e_debug(storage->event,
			"Created directory for binaries: %s", bin_path);
		return 1;
	}

	switch (errno) {
	case EEXIST:
		return 0;
	case ENOENT:
		sieve_storage_set_critical(storage,
			"Directory for binaries was deleted while it was being created");
		break;
	case EACCES:
		sieve_storage_set_critical(storage,
			"%s", eacces_error_get_creating("mkdir_parents_chgrp",
							bin_path));
		break;
	default:
		sieve_storage_set_critical(storage,
			"mkdir_parents_chgrp(%s) failed: %m", bin_path);
		break;
	}

	return -1;
}

/*
 * Properties
 */

int sieve_storage_is_singular(struct sieve_storage *storage)
{
	if (storage->v.is_singular == NULL)
		return 1;
	return storage->v.is_singular(storage);
}

int sieve_storage_get_last_change(struct sieve_storage *storage,
				  time_t *last_change_r)
{
	i_assert(storage->v.get_last_change != NULL);
	return storage->v.get_last_change(storage, last_change_r);
}

void sieve_storage_set_modified(struct sieve_storage *storage, time_t mtime)
{
	if (storage->v.set_modified == NULL)
		return;

	storage->v.set_modified(storage, mtime);
}

/*
 * Comparison
 */

int sieve_storage_cmp(const struct sieve_storage *storage1,
		      const struct sieve_storage *storage2)
{
	int ret;

	if (storage1 == storage2)
		return 0;
	if (storage1 == NULL || storage2 == NULL)
		return (storage1 == NULL ? -1 : 1);
	if (storage1->storage_class != storage2->storage_class) {
		return (storage1->storage_class > storage2->storage_class ?
			1 : -1);
	}
	ret = null_strcmp(storage1->type, storage2->type);
	if (ret != 0)
		return (ret > 0 ? 1 : -1);
	return null_strcmp(storage1->name, storage2->name);
}

unsigned int sieve_storage_hash(const struct sieve_storage *storage)
{
	unsigned int hash = 0;

	hash ^= POINTER_CAST_TO(storage->storage_class, unsigned int);
	hash ^= str_hash(storage->type);
	hash ^= str_hash(storage->name);

	return hash;
}

/*
 * Script access
 */

int sieve_storage_get_script_direct(struct sieve_storage *storage,
				    const char *name,
				    struct sieve_script **script_r,
				    enum sieve_error *error_code_r)
{
	int ret;

	/* Validate script name */
	if (name != NULL && !sieve_script_name_is_valid(name)) {
		sieve_storage_set_error(storage,
			SIEVE_ERROR_BAD_PARAMS,
			"Invalid script name '%s'.",
			str_sanitize(name, 80));
		*error_code_r = storage->error_code;
		return -1;
	}
	if (name == NULL)
		name = storage->script_name;

	i_assert(storage->v.get_script != NULL);
	ret = storage->v.get_script(storage, name, script_r);
	i_assert(ret <= 0);
	if (ret < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
		*error_code_r = storage->error_code;
	}
	return ret;
}

static int
sieve_storage_get_default_script(struct sieve_storage *storage,
				 const char *name,
				 struct sieve_script **script_r,
				 enum sieve_error *error_code_r)
{
	struct sieve_storage *def_storage;
	int ret;

	if (*error_code_r != SIEVE_ERROR_NOT_FOUND ||
	    (storage->flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) != 0 ||
	    !sieve_storage_is_personal(storage))
		return -1;

	/* Not found; if this name maps to the default script,
	   try to access that instead */
	e_debug(storage->event, "Trying default script instead");

	/* Failed; try using default script location
	   (not for temporary failures, read/write access, or dsync) */
	ret = sieve_storage_create_default_for(storage, &def_storage,
					       error_code_r, NULL);
	if (ret < 0)
		return -1;

	if (strcmp(def_storage->script_name, name) != 0) {
		sieve_storage_set_error(storage,
			SIEVE_ERROR_NOT_FOUND,
			"Default script '%s' not found",
			str_sanitize(name, 80));
		*error_code_r = storage->error_code;
		sieve_storage_unref(&def_storage);
		return -1;
	}

	struct sieve_script *def_script;

	ret = sieve_storage_get_script_direct(def_storage, name,
					      &def_script, error_code_r);
	if (ret < 0)
		sieve_storage_copy_error(storage, def_storage);
	sieve_storage_unref(&def_storage);
	if (ret < 0)
		return -1;
	i_assert(def_script != NULL);

	*script_r = def_script;
	return 0;
}

int sieve_storage_get_script(struct sieve_storage *storage, const char *name,
			     struct sieve_script **script_r,
			     enum sieve_error *error_code_r)
{
	*script_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);
	sieve_storage_clear_error(storage);

	if (sieve_storage_get_script_direct(storage, name,
					    script_r, error_code_r) >= 0)
		return 0;

	/* Try default instead if appropriate */

	return sieve_storage_get_default_script(storage, name,
						script_r, error_code_r);
}

int sieve_storage_open_script(struct sieve_storage *storage, const char *name,
			      struct sieve_script **script_r,
			      enum sieve_error *error_code_r)
{
	struct sieve_script *script;

	*script_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);
	sieve_storage_clear_error(storage);

	if (sieve_storage_get_script(storage, name, &script, error_code_r) < 0)
		return -1;
	if (sieve_script_open(script, error_code_r) == 0) {
		*script_r = script;
		return 0;
	}
	sieve_script_unref(&script);

	/* Try default instead if appropriate */

	if (sieve_storage_get_default_script(storage, name,
					     &script, error_code_r) < 0)
		return -1;
	if (sieve_script_open(script, error_code_r) < 0) {
		sieve_script_unref(&script);
		return -1;
	}
	*script_r = script;
	return 0;
}

static int
sieve_storage_check_script_direct(struct sieve_storage *storage,
				  const char *name,
				  enum sieve_error *error_code_r)
{
	struct sieve_script *script;
	int ret;

	sieve_error_args_init(&error_code_r, NULL);
	sieve_storage_clear_error(storage);

	if (sieve_storage_get_script_direct(storage, name,
					    &script, error_code_r) < 0)
		return (*error_code_r == SIEVE_ERROR_NOT_FOUND ? 0 : -1);

	ret = sieve_script_open(script, error_code_r);
	sieve_script_unref(&script);
	return (ret >= 0 ? 1 :
		(*error_code_r == SIEVE_ERROR_NOT_FOUND ? 0 : -1));
}

int sieve_storage_check_script(struct sieve_storage *storage, const char *name,
			       enum sieve_error *error_code_r)
{
	struct sieve_script *script;

	sieve_error_args_init(&error_code_r, NULL);
	sieve_storage_clear_error(storage);

	if (sieve_storage_open_script(storage, name, &script, error_code_r) < 0)
		return (*error_code_r == SIEVE_ERROR_NOT_FOUND ? 0 : -1);

	sieve_script_unref(&script);
	return 1;
}

/*
 * Active script
 */

static int
sieve_storage_active_script_do_get_name(struct sieve_storage *storage,
					const char **name_r, bool *default_r)
					ATTR_NULL(3)
{
	int ret;

	if (default_r != NULL)
		*default_r = FALSE;

	i_assert(storage->v.active_script_get_name != NULL);
	ret = storage->v.active_script_get_name(storage, name_r);
	if (ret < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
		if (storage->error_code == SIEVE_ERROR_NOT_FOUND) {
			sieve_storage_clear_error(storage);
			ret = 0;
		}
	}

	if (ret != 0 ||
	    (storage->flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) != 0)
		return ret;

	struct sieve_storage *def_storage;

	/* Failed; try using default script location
	   (not for temporary failures, read/write access, or dsync) */
	ret = sieve_storage_create_default_for(storage, &def_storage,
					       NULL, NULL);
	if (ret < 0)
		return -1;

	*name_r = def_storage->script_name;

	ret = sieve_storage_check_script(def_storage,
					 def_storage->script_name, NULL);
	if (ret < 0) {
		sieve_storage_copy_error(storage, def_storage);
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
	}
	sieve_storage_unref(&def_storage);
	if (ret <= 0)
		return ret;

	if (default_r != NULL)
		*default_r = TRUE;
	return 1;
}

int sieve_storage_active_script_get_name(struct sieve_storage *storage,
					 const char **name_r)
{
	return sieve_storage_active_script_do_get_name(storage, name_r, NULL);
}

int sieve_storage_active_script_is_default(struct sieve_storage *storage)
{
	const char *name;
	bool is_default = FALSE;
	int ret;

	ret = sieve_storage_active_script_do_get_name(storage, &name,
						      &is_default);
	return (ret < 0 ? -1 : (is_default ? 1 : 0));
}

int sieve_storage_active_script_open(struct sieve_storage *storage,
				     struct sieve_script **script_r,
				     enum sieve_error *error_code_r)
{
	struct sieve_script *script = NULL;
	int ret;

	*script_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);
	sieve_storage_clear_error(storage);

	i_assert(storage->v.active_script_open != NULL);
	ret = storage->v.active_script_open(storage, &script);
	i_assert(ret <= 0);
	i_assert(ret == 0 ||
		 (storage->error_code != SIEVE_ERROR_NONE &&
		  storage->error != NULL));

	if (ret == 0 || storage->error_code != SIEVE_ERROR_NOT_FOUND ||
	    (storage->flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) != 0) {
		if (ret < 0)
			*error_code_r = storage->error_code;
		*script_r = script;
		return ret;
	}

	struct sieve_storage *def_storage;

	/* Try default script location */
	ret = sieve_storage_create_default_for(storage, &def_storage,
					       error_code_r, NULL);
	if (ret < 0)
		return -1;

	ret = sieve_storage_open_script(def_storage, NULL,
					script_r, error_code_r);
	if (ret < 0)
		sieve_storage_copy_error(storage, def_storage);
	sieve_storage_unref(&def_storage);
	return ret;
}

int sieve_storage_deactivate(struct sieve_storage *storage, time_t mtime)
{
	int ret;

	i_assert((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0);

	sieve_storage_clear_error(storage);

	i_assert(storage->v.deactivate != NULL);
	ret = storage->v.deactivate(storage);

	if (ret >= 0) {
		struct event_passthrough *e =
			event_create_passthrough(storage->event)->
			set_name("sieve_storage_deactivated");
		e_debug(e->event(), "Storage deactivated");

		sieve_storage_set_modified(storage, mtime);
		(void)sieve_storage_sync_deactivate(storage);
	} else {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);

		struct event_passthrough *e =
			event_create_passthrough(storage->event)->
			add_str("error", storage->error)->
			set_name("sieve_storage_deactivated");
		e_debug(e->event(), "Failed to deactivate storage: %s",
			storage->error);
	}

	return ret;
}

int sieve_storage_active_script_get_last_change(struct sieve_storage *storage,
						time_t *last_change_r)
{
	i_assert(storage->v.active_script_get_last_change != NULL);

	return storage->v.active_script_get_last_change(storage, last_change_r);
}

/*
 * Listing scripts
 */

int sieve_storage_list_init(struct sieve_storage *storage,
			    struct sieve_storage_list_context **lctx_r)
{
	struct sieve_storage_list_context *lctx;

	*lctx_r = NULL;
	sieve_storage_clear_error(storage);

	i_assert(storage->v.list_init != NULL);
	if (storage->v.list_init(storage, &lctx) < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
		return -1;
	}

	lctx->storage = storage;
	sieve_storage_ref(storage);

	if ((storage->flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) != 0) {
		/* No default script involved; return right away */
		*lctx_r = lctx;
		return 0;
	}

	/* May need to list default script as well */

	enum sieve_error error_code;

	if (sieve_storage_create_default_for(storage, &lctx->def_storage,
					     &error_code, NULL) < 0 &&
	    error_code != SIEVE_ERROR_NOT_FOUND)
		return -1;

	*lctx_r = lctx;
	return 0;
}

const char *
sieve_storage_list_next(struct sieve_storage_list_context *lctx, bool *active_r)
{
	struct sieve_storage *storage = lctx->storage;
	const char *scriptname;
	bool script_active = FALSE;

	sieve_storage_clear_error(storage);

	i_assert(storage->v.list_next != NULL);
	scriptname = storage->v.list_next(lctx, &script_active);

	i_assert(!script_active || !lctx->seen_active);
	if (script_active)
		lctx->seen_active = TRUE;

	struct sieve_storage *def_storage = lctx->def_storage;
	bool have_default = (def_storage != NULL &&
			     def_storage->script_name != NULL);

	if (scriptname != NULL) {
		/* Remember when we see that the storage has its own script for
		   default */
		if (have_default &&
		    strcmp(scriptname, def_storage->script_name) == 0)
			lctx->seen_default = TRUE;

	} else if (have_default && !lctx->seen_default &&
		   sieve_storage_check_script(def_storage, NULL, NULL) > 0) {

		/* Return default script at the end if it was not listed
		   thus far (storage backend has no script under default
		   name) */
		scriptname = def_storage->script_name;
		lctx->seen_default = TRUE;

		/* Mark default as active if no normal script is active */
		if (!lctx->seen_active) {
			script_active = TRUE;
			lctx->seen_active = TRUE;
		}
	}

	if (active_r != NULL)
		*active_r = script_active;
	return scriptname;
}

int sieve_storage_list_deinit(struct sieve_storage_list_context **_lctx)
{
	struct sieve_storage_list_context *lctx = *_lctx;

	if (lctx == NULL)
		return 0;
	*_lctx = NULL;

	struct sieve_storage *storage = lctx->storage;
	struct sieve_storage *def_storage = lctx->def_storage;
	int ret;

	i_assert(storage->v.list_deinit != NULL);
	ret = storage->v.list_deinit(lctx);
	i_assert(ret >= 0 ||
		 (storage->error_code != SIEVE_ERROR_NONE &&
		  storage->error != NULL));

	sieve_storage_unref(&def_storage);
	sieve_storage_unref(&storage);
	return ret;
}

/*
 * Saving scripts
 */

static struct event *
sieve_storage_save_create_event(struct sieve_storage *storage,
				const char *scriptname) ATTR_NULL(2)
{
	struct event *event;

	event = event_create(storage->event);
	event_add_str(event, "script_name", scriptname);
	if (scriptname == NULL) {
		event_set_append_log_prefix(event, "save: ");
	} else {
		event_set_append_log_prefix(
			event, t_strdup_printf("script '%s': save: ",
					       scriptname));
	}

	return event;
}

static void sieve_storage_save_cleanup(struct sieve_storage_save_context *sctx)
{
	sieve_script_unref(&sctx->scriptobject);
}

static void sieve_storage_save_deinit(struct sieve_storage_save_context **_sctx)
{
	struct sieve_storage_save_context *sctx = *_sctx;

	*_sctx = NULL;
	if (sctx == NULL)
		return;

	sieve_storage_save_cleanup(sctx);
	event_unref(&sctx->event);
	pool_unref(&sctx->pool);
}

struct sieve_storage_save_context *
sieve_storage_save_init(struct sieve_storage *storage, const char *scriptname,
			struct istream *input)
{
	struct sieve_storage_save_context *sctx;

	sieve_storage_clear_error(storage);

	if (scriptname != NULL) {
		/* Validate script name */
		if (!sieve_script_name_is_valid(scriptname)) {
			sieve_storage_set_error(storage,
				SIEVE_ERROR_BAD_PARAMS,
				"Invalid Sieve script name '%s'.",
				str_sanitize(scriptname, 80));
			return NULL;
		}
	}

	i_assert((storage->flags & SIEVE_STORAGE_FLAG_READWRITE) != 0);

	i_assert(storage->v.save_alloc != NULL);
	sctx = storage->v.save_alloc(storage);
	sctx->storage = storage;

	sctx->event = sieve_storage_save_create_event(storage, scriptname);

	struct event_passthrough *e =
		event_create_passthrough(sctx->event)->
		set_name("sieve_storage_save_started");
	e_debug(e->event(), "Started saving script");

	i_assert(storage->v.save_init != NULL);
	if ((storage->v.save_init(sctx, scriptname, input)) < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);

		struct event_passthrough *e =
			event_create_passthrough(sctx->event)->
			add_str("error", storage->error)->
			set_name("sieve_storage_save_finished");
		e_debug(e->event(), "Failed to save script: %s",
			storage->error);

		sieve_storage_save_deinit(&sctx);
		return NULL;
	}

	sctx->mtime = (time_t)-1;

	i_assert(sctx->input != NULL);

	return sctx;
}

int sieve_storage_save_continue(struct sieve_storage_save_context *sctx)
{
	struct sieve_storage *storage = sctx->storage;
	int ret;

	sieve_storage_clear_error(storage);

	i_assert(storage->v.save_continue != NULL);
	ret = storage->v.save_continue(sctx);
	if (ret < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);
		sctx->failed = TRUE;
	}
	return ret;
}

int sieve_storage_save_finish(struct sieve_storage_save_context *sctx)
{
	struct sieve_storage *storage = sctx->storage;
	int ret;

	sieve_storage_clear_error(storage);

	i_assert(!sctx->finished);
	sctx->finished = TRUE;

	i_assert(storage->v.save_finish != NULL);
	ret = storage->v.save_finish(sctx);
	if (ret < 0) {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);

		struct event_passthrough *e =
			event_create_passthrough(sctx->event)->
			add_str("error", storage->error)->
			set_name("sieve_storage_save_finished");
		e_debug(e->event(), "Failed to upload script: %s",
			storage->error);

		sctx->failed = TRUE;
	}
	return ret;
}

void sieve_storage_save_set_mtime(struct sieve_storage_save_context *sctx,
				  time_t mtime)
{
	sctx->mtime = mtime;
}

struct sieve_script *
sieve_storage_save_get_tempscript(struct sieve_storage_save_context *sctx)
{
	struct sieve_storage *storage = sctx->storage;

	if (sctx->failed)
		return NULL;

	if (sctx->scriptobject != NULL)
		return sctx->scriptobject;

	sieve_storage_clear_error(storage);

	i_assert(storage->v.save_get_tempscript != NULL);
	sctx->scriptobject = storage->v.save_get_tempscript(sctx);

	i_assert(sctx->scriptobject != NULL ||
		 (storage->error_code != SIEVE_ERROR_NONE &&
		  storage->error != NULL));
	return sctx->scriptobject;
}

bool sieve_storage_save_will_activate(struct sieve_storage_save_context *sctx)
{
	struct sieve_storage *storage = sctx->storage;

	if (sctx->scriptname == NULL)
		return FALSE;

	sieve_storage_clear_error(storage);

	if (sctx->active_scriptname == NULL) {
		const char *scriptname;

		if (sieve_storage_active_script_get_name(sctx->storage,
							 &scriptname) > 0) {
			sctx->active_scriptname =
				p_strdup(sctx->pool, scriptname);
		}
	}

 	/* Is the requested script active? */
	return (sctx->active_scriptname != NULL &&
		strcmp(sctx->scriptname, sctx->active_scriptname) == 0);
}

static int
sieve_storage_save_is_activating_default(
	struct sieve_storage_save_context *sctx)
{
	struct sieve_storage *storage = sctx->storage;

	if ((storage->flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) != 0)
		return 0;
	if (!sieve_storage_save_will_activate(sctx))
		return 0;

	struct sieve_storage *def_storage;
	enum sieve_error error_code;
	int ret = 0;

	if (sieve_storage_create_default_for(storage, &def_storage,
					     &error_code, NULL) < 0) {
		if (error_code == SIEVE_ERROR_NOT_FOUND)
			return 0;
		return -1;
	}

	if (strcmp(sctx->scriptname, def_storage->script_name) == 0) {
		ret = sieve_storage_check_script_direct(
			storage, def_storage->script_name, &error_code);
		if (ret == 0 ||
		    (ret < 0 && error_code == SIEVE_ERROR_NOT_FOUND))
			ret = 1;
		else if (ret > 0)
			ret = 0;
	}

	sieve_storage_unref(&def_storage);
	return ret;
}

int sieve_storage_save_commit(struct sieve_storage_save_context **_sctx)
{
	struct sieve_storage_save_context *sctx = *_sctx;
	struct sieve_storage *storage;
	const char *scriptname;
	bool default_activate = FALSE;
	int ret;

	*_sctx = NULL;
	if (sctx == NULL)
		return 0;

	storage = sctx->storage;
	scriptname = sctx->scriptname;
	sieve_storage_clear_error(storage);

	i_assert(!sctx->failed);
	i_assert(sctx->finished);
	i_assert(sctx->scriptname != NULL);

	/* Check whether we're replacing the default active script */
	ret = sieve_storage_save_is_activating_default(sctx);
	if (ret < 0)
		return -1;
	default_activate = (ret > 0);

	sieve_storage_save_cleanup(sctx);

	i_assert(storage->v.save_commit != NULL);
	ret = storage->v.save_commit(sctx);
	i_assert(ret >= 0 ||
		 (storage->error_code != SIEVE_ERROR_NONE &&
		  storage->error != NULL));

	/* Implicitly activate it when we're replacing the default
	   active script */
	if (ret >= 0 && default_activate) {
		struct sieve_script *script;
		enum sieve_error error_code;

		if (sieve_storage_open_script(storage, scriptname,
					      &script, &error_code) < 0) {
			/* Somehow not actually saved */
			ret = (error_code == SIEVE_ERROR_NOT_FOUND ? 0 : -1);
		} else if (sieve_script_activate(script, (time_t)-1) < 0) {
			/* Failed to activate; roll back */
			ret = -1;
			(void)sieve_script_delete(script, TRUE);
		}
		sieve_script_unref(&script);

		if (ret < 0) {
			e_error(sctx->event,
				"Failed to implicitly activate script '%s' "
				"while replacing the default active script",
				scriptname);
		}
	}

	if (ret >= 0) {
		struct event_passthrough *e =
			event_create_passthrough(sctx->event)->
			set_name("sieve_storage_save_finished");
		e_debug(e->event(), "Finished saving script");

		/* set INBOX mailbox attribute */
		(void)sieve_storage_sync_script_save(storage, scriptname);
	} else {
		struct event_passthrough *e =
			event_create_passthrough(sctx->event)->
			add_str("error", storage->error)->
			set_name("sieve_storage_save_finished");

		e_debug(e->event(), "Failed to save script: %s",
			storage->error);
	}

	sieve_storage_save_deinit(&sctx);
	return ret;
}

void sieve_storage_save_cancel(struct sieve_storage_save_context **_sctx)
{
	struct sieve_storage_save_context *sctx = *_sctx;
	struct sieve_storage *storage;

	*_sctx = NULL;
	if (sctx == NULL)
		return;

	storage = sctx->storage;

	sctx->failed = TRUE;

	sieve_storage_save_cleanup(sctx);

	if (!sctx->finished)
		(void)sieve_storage_save_finish(sctx);

	struct event_passthrough *e =
		event_create_passthrough(sctx->event)->
		add_str("error", "Canceled")->
		set_name("sieve_storage_save_finished");
	e_debug(e->event(), "Canceled saving script");

	i_assert(storage->v.save_cancel != NULL);
	storage->v.save_cancel(sctx);

	sieve_storage_save_deinit(&sctx);
}

int sieve_storage_save_as_active(struct sieve_storage *storage,
				 struct istream *input, time_t mtime)
{
	struct event *event;
	int ret;

	sieve_storage_clear_error(storage);

	event = event_create(storage->event);
	event_set_append_log_prefix(event, "active script: save: ");

	struct event_passthrough *e =
		event_create_passthrough(event)->
		set_name("sieve_storage_save_started");
	e_debug(e->event(), "Started saving active script");

	i_assert(storage->v.save_as_active != NULL);
	ret = storage->v.save_as_active(storage, input, mtime);

	if (ret >= 0) {
		struct event_passthrough *e =
			event_create_passthrough(event)->
			set_name("sieve_storage_save_finished");
		e_debug(e->event(), "Finished saving active script");
	} else {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);

		struct event_passthrough *e =
			event_create_passthrough(event)->
			add_str("error", storage->error)->
			set_name("sieve_storage_save_finished");
		e_debug(e->event(), "Failed to save active script: %s",
			storage->error);
	}

	event_unref(&event);
	return ret;
}

int sieve_storage_save_as(struct sieve_storage *storage, struct istream *input,
			  const char *name)
{
	struct event *event;
	int ret;

	sieve_storage_clear_error(storage);

	event = sieve_storage_save_create_event(storage, name);

	struct event_passthrough *e =
		event_create_passthrough(event)->
		set_name("sieve_storage_save_started");
	e_debug(e->event(), "Started saving script");

	i_assert(storage->v.save_as != NULL);
	ret = storage->v.save_as(storage, input, name);

	if (ret >= 0) {
		struct event_passthrough *e =
			event_create_passthrough(event)->
			set_name("sieve_storage_save_finished");
		e_debug(e->event(), "Finished saving sieve script");
	} else {
		i_assert(storage->error_code != SIEVE_ERROR_NONE);
		i_assert(storage->error != NULL);

		struct event_passthrough *e =
			event_create_passthrough(event)->
			add_str("error", storage->error)->
			set_name("sieve_storage_save_finished");
		e_debug(e->event(), "Failed to save script: %s",
			storage->error);
	}

	event_unref(&event);
	return ret;
}

/*
 * Checking quota
 */

bool sieve_storage_quota_validsize(struct sieve_storage *storage, size_t size,
				   uint64_t *limit_r)
{
	uint64_t max_size;

	max_size = sieve_max_script_size(storage->svinst);
	if (max_size > 0 && size > max_size) {
		*limit_r = max_size;
		return FALSE;
	}

	return TRUE;
}

uint64_t sieve_storage_quota_max_script_size(struct sieve_storage *storage)
{
	return sieve_max_script_size(storage->svinst);
}

int sieve_storage_quota_havespace(struct sieve_storage *storage,
				  const char *scriptname, size_t size,
				  enum sieve_storage_quota *quota_r,
				  uint64_t *limit_r)
{
	*quota_r = SIEVE_STORAGE_QUOTA_NONE;
	*limit_r = 0;

	/* Check the script size */
	if (!sieve_storage_quota_validsize(storage, size, limit_r)) {
		*quota_r = SIEVE_STORAGE_QUOTA_MAXSIZE;
		return 0;
	}

	/* Do we need to scan the storage (quota enabled) ? */
	if (storage->max_scripts == 0 && storage->max_storage == 0)
		return 1;

	if (storage->v.quota_havespace == NULL)
		return 1;

	return storage->v.quota_havespace(storage, scriptname, size,
					  quota_r, limit_r);
}

/*
 * Properties
 */

const char *sieve_storage_name(const struct sieve_storage *storage)
{
	return storage->name;
}

bool sieve_storage_is_default(const struct sieve_storage *storage)
{
	return storage->is_default;
}

bool sieve_storage_is_personal(struct sieve_storage *storage)
{
	return (strcasecmp(storage->type, SIEVE_STORAGE_TYPE_PERSONAL) == 0);
}

/*
 * Error handling
 */

void sieve_storage_clear_error(struct sieve_storage *storage)
{
	i_free(storage->error);
	storage->error_code = SIEVE_ERROR_NONE;
	storage->error = NULL;
}

void sieve_storage_set_error(struct sieve_storage *storage,
			     enum sieve_error error_code, const char *fmt, ...)
{
	va_list va;

	sieve_storage_clear_error(storage);

	if (fmt != NULL) {
		va_start(va, fmt);
		storage->error = i_strdup_vprintf(fmt, va);
		va_end(va);
	}

	storage->error_code = error_code;
}

void sieve_storage_copy_error(struct sieve_storage *storage,
			      const struct sieve_storage *source)
{
	sieve_storage_clear_error(storage);
	storage->error = i_strdup(source->error);
	storage->error_code = source->error_code;
}

void sieve_storage_set_internal_error(struct sieve_storage *storage)
{
	const char *error;

	sieve_storage_clear_error(storage);
	sieve_error_create_internal(&storage->error_code, &error);
	storage->error = i_strdup(error);
}

void sieve_storage_set_critical(struct sieve_storage *storage,
				const char *fmt, ...)
{
	va_list va;

	if (fmt != NULL) {
		if ((storage->flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) == 0) {
			va_start(va, fmt);
			e_error(storage->svinst->event, "%s storage: %s",
				storage->driver_name,
				t_strdup_vprintf(fmt, va));
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

void sieve_storage_set_not_found_error(struct sieve_storage *storage,
				       const char *name)
{
	enum sieve_error error_code;
	const char *error;

	sieve_storage_clear_error(storage);
	name = (name == NULL || *name == '\0' ? storage->script_name : name);
	sieve_error_create_script_not_found(name, &error_code, &error);
	storage->error_code = error_code;
	storage->error = i_strdup(error);
}

const char *
sieve_storage_get_last_error(struct sieve_storage *storage,
			     enum sieve_error *error_code_r)
{
	/* We get here only in error situations, so we have to return some
	   error. If storage->error is NULL, it means we forgot to set it at
	   some point..
	 */

	if (error_code_r != NULL)
		*error_code_r = storage->error_code;

	return storage->error != NULL ? storage->error : "Unknown error";
}

/*
 * Storage sequence
 */

int sieve_storage_sequence_create(struct sieve_instance *svinst,
				  struct event *event_parent,
				  const char *cause, const char *type,
				  struct sieve_storage_sequence **sseq_r,
				  enum sieve_error *error_code_r,
				  const char **error_r)
{
	const struct sieve_storage_settings *storage_set;
	const char *const *storage_names;
	unsigned int storage_count;
	const char *error;

	*sseq_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	if (settings_get(event_parent, &sieve_storage_setting_parser_info,
			 SETTINGS_GET_FLAG_SORT_FILTER_ARRAYS,
			 &storage_set, &error) < 0) {
		e_error(event_parent, "%s", error);
		sieve_error_create_internal(error_code_r, error_r);
		return -1;
	}
	if (array_is_created(&storage_set->storages))
		storage_names = array_get(&storage_set->storages,
					  &storage_count);
	else {
		storage_names = empty_str_array;
		storage_count = 0;
	}

	struct sieve_storage_sequence *sseq;

	sseq = i_new(struct sieve_storage_sequence, 1);
	sseq->svinst = svinst;
	sseq->cause = i_strdup(cause);
	sseq->type = i_strdup(type);
	sseq->storage_set = storage_set;
	sseq->storage_names = p_strarray_dup(default_pool, storage_names);
	sseq->storage_count = storage_count;

	sseq->event_parent = event_parent;
	event_ref(event_parent);

	*sseq_r = sseq;
	return 0;
}

int sieve_storage_sequence_next(struct sieve_storage_sequence *sseq,
				struct sieve_storage **storage_r,
				enum sieve_error *error_code_r,
				const char **error_r)
{
	struct sieve_instance *svinst = sseq->svinst;
	int ret;

	*storage_r = NULL;
	sieve_error_args_init(&error_code_r, &error_r);

	while (sseq->storage_index < sseq->storage_count) {
		unsigned int index = sseq->storage_index++;

		ret = sieve_storage_init(svinst, sseq->event_parent,
					 sseq->cause, sseq->type,
					 sseq->storage_names[index], TRUE, 0,
					 storage_r, error_code_r, error_r);
		if (ret < 0) {
			if (*error_code_r == SIEVE_ERROR_NOT_FOUND) {
				*error_code_r = SIEVE_ERROR_NONE;
				*error_r = NULL;
				continue;
			}
			return -1;
		}
		if (ret > 0) {
			i_assert(*storage_r != NULL);
			return 1;
		}
	}

	return 0;
}

void sieve_storage_sequence_free(struct sieve_storage_sequence **_sseq)
{
	struct sieve_storage_sequence *sseq = *_sseq;

	if (sseq == NULL)
		return;
	*_sseq = NULL;

	event_unref(&sseq->event_parent);
	i_free(sseq->cause);
	i_free(sseq->type);
	settings_free(sseq->storage_set);
	i_free(sseq->storage_names);
	i_free(sseq);
}
