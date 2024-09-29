/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
//#include "ldap.h"

#include "sieve-common.h"

#include "sieve-ldap-storage.h"
#include "sieve-ldap-storage-settings.h"

#if defined(SIEVE_BUILTIN_LDAP) || defined(PLUGIN_BUILD)

#include "sieve-error.h"

#ifndef PLUGIN_BUILD
const struct sieve_storage sieve_ldap_storage;
#else
const struct sieve_storage sieve_ldap_storage_plugin;
#endif

/*
 * Storage class
 */

static struct sieve_storage *sieve_ldap_storage_alloc(void)
{
	struct sieve_ldap_storage *lstorage;
	pool_t pool;

	pool = pool_alloconly_create("sieve_ldap_storage", 1024);
	lstorage = p_new(pool, struct sieve_ldap_storage, 1);
#ifndef PLUGIN_BUILD
	lstorage->storage = sieve_ldap_storage;
#else
	lstorage->storage = sieve_ldap_storage_plugin;
#endif
	lstorage->storage.pool = pool;

	return &lstorage->storage;
}

static int
sieve_ldap_storage_init(struct sieve_storage *storage,
			const char *const *options)
{
	struct sieve_ldap_storage *lstorage =
		container_of(storage, struct sieve_ldap_storage, storage);
	struct sieve_instance *svinst = storage->svinst;
	const char *value;

	if (options != NULL) {
		while (*options != NULL) {
			const char *option = *options;

			if (str_begins_icase(option, "user=", &value) &&
			    *value != '\0') {
				/* Ignore */
			} else {
				sieve_storage_set_critical(
					storage, "Invalid option '%s'", option);
				return -1;
			}

			options++;
		}
	}

	e_debug(storage->event, "user=%s, config=%s",
		svinst->username, storage->location);

	if (sieve_ldap_storage_read_settings(lstorage, storage->location) < 0)
		return -1;

	lstorage->config_file = p_strdup(storage->pool, storage->location);
	lstorage->conn = sieve_ldap_db_init(lstorage);

	storage->location = p_strconcat(
		storage->pool, SIEVE_LDAP_STORAGE_DRIVER_NAME, ":",
		storage->location, ";user=", svinst->username, NULL);

	return 0;
}

static void sieve_ldap_storage_destroy(struct sieve_storage *storage)
{
	struct sieve_ldap_storage *lstorage =
		container_of(storage, struct sieve_ldap_storage, storage);

	sieve_ldap_db_unref(&lstorage->conn);
}

/*
 * Script access
 */

static int
sieve_ldap_storage_get_script(struct sieve_storage *storage, const char *name,
			      struct sieve_script **script_r)
{
	struct sieve_ldap_storage *lstorage =
		container_of(storage, struct sieve_ldap_storage, storage);
	struct sieve_ldap_script *lscript;

	T_BEGIN {
		lscript = sieve_ldap_script_init(lstorage, name);
	} T_END;

	if (lscript == NULL)
		return -1;
	*script_r = &lscript->script;
	return 0;
}

/*
 * Active script
 */

static int
sieve_ldap_storage_active_script_open(struct sieve_storage *storage,
				      struct sieve_script **script_r)
{
	struct sieve_ldap_storage *lstorage =
		container_of(storage, struct sieve_ldap_storage, storage);
	struct sieve_ldap_script *lscript;

	lscript = sieve_ldap_script_init(lstorage, storage->script_name);
	if (sieve_script_open(&lscript->script, NULL) < 0) {
		struct sieve_script *script = &lscript->script;
		sieve_script_unref(&script);
		return -1;
	}

	*script_r = &lscript->script;
	return 0;
}

int sieve_ldap_storage_active_script_get_name(struct sieve_storage *storage,
					      const char **name_r)
{
	if (storage->script_name != NULL)
		*name_r = storage->script_name;
	else
		*name_r = SIEVE_LDAP_SCRIPT_DEFAULT;
	return 0;
}

/*
 * Driver definition
 */

#ifndef PLUGIN_BUILD
const struct sieve_storage sieve_ldap_storage = {
#else
const struct sieve_storage sieve_ldap_storage_plugin = {
#endif
	.driver_name = SIEVE_LDAP_STORAGE_DRIVER_NAME,
	.version = 0,
	.v = {
		.alloc = sieve_ldap_storage_alloc,
		.init = sieve_ldap_storage_init,
		.destroy = sieve_ldap_storage_destroy,

		.get_script = sieve_ldap_storage_get_script,

		.script_sequence_init = sieve_ldap_script_sequence_init,
		.script_sequence_next = sieve_ldap_script_sequence_next,
		.script_sequence_destroy = sieve_ldap_script_sequence_destroy,

		.active_script_get_name = sieve_ldap_storage_active_script_get_name,
		.active_script_open = sieve_ldap_storage_active_script_open,

		// FIXME: impement management interface
	},
};

#ifndef SIEVE_BUILTIN_LDAP
/* Building a plugin */

const char *sieve_storage_ldap_plugin_version = PIGEONHOLE_ABI_VERSION;

void sieve_storage_ldap_plugin_load(struct sieve_instance *svinst,
				    void **context);
void sieve_storage_ldap_plugin_unload(struct sieve_instance *svinst,
				      void *context);
void sieve_storage_ldap_plugin_init(void);
void sieve_storage_ldap_plugin_deinit(void);

void sieve_storage_ldap_plugin_load(struct sieve_instance *svinst,
				    void **context ATTR_UNUSED)
{
	sieve_storage_class_register(svinst, &sieve_ldap_storage_plugin);

	e_debug(svinst->event,
		"Sieve LDAP storage plugin for %s version %s loaded",
		PIGEONHOLE_NAME, PIGEONHOLE_VERSION_FULL);
}

void sieve_storage_ldap_plugin_unload(struct sieve_instance *svinst ATTR_UNUSED,
				      void *context ATTR_UNUSED)
{
	sieve_storage_class_unregister(svinst, &sieve_ldap_storage_plugin);
}

void sieve_storage_ldap_plugin_init(void)
{
	/* Nothing */
}

void sieve_storage_ldap_plugin_deinit(void)
{
	/* Nothing */
}
#endif

#else /* !defined(SIEVE_BUILTIN_LDAP) && !defined(PLUGIN_BUILD) */
const struct sieve_storage sieve_ldap_storage = {
	.driver_name = SIEVE_LDAP_STORAGE_DRIVER_NAME,
};
#endif
