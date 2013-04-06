/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "hash.h"
#include "imem.h"
#include "strfuncs.h"

#include "sieve-common.h"

#include "testsuite-common.h"
#include "testsuite-settings.h"

struct testsuite_setting {
	char *identifier;
	char *value;
};

static HASH_TABLE(const char *, struct testsuite_setting *) settings;

static const char *testsuite_setting_get
	(void *context, const char *identifier);

void testsuite_settings_init(void)
{
	hash_table_create(&settings, default_pool, 0, str_hash, strcmp);

	sieve_tool_set_setting_callback(sieve_tool, testsuite_setting_get, NULL);
}

void testsuite_settings_deinit(void)
{
	struct hash_iterate_context *itx =
		hash_table_iterate_init(settings);
	const char *key;
	struct testsuite_setting *setting;

	while ( hash_table_iterate(itx, settings, &key, &setting) ) {
		i_free(setting->identifier);
		i_free(setting->value);
		i_free(setting);
	}

	hash_table_iterate_deinit(&itx);

	hash_table_destroy(&settings);
}

static const char *testsuite_setting_get
(void *context ATTR_UNUSED, const char *identifier)
{
	struct testsuite_setting *setting =
		hash_table_lookup(settings, identifier);

	if ( setting == NULL ) {
		return NULL;
	}

	return setting->value;
}

void testsuite_setting_set(const char *identifier, const char *value)
{
	struct testsuite_setting *setting =
		hash_table_lookup(settings, identifier);

	if ( setting != NULL ) {
		i_free(setting->value);
		setting->value = i_strdup(value);
	} else {
		setting = i_new(struct testsuite_setting, 1);
		setting->identifier = i_strdup(identifier);
		setting->value = i_strdup(value);

		hash_table_insert(settings, identifier, setting);
	}
}

void testsuite_setting_unset(const char *identifier)
{
	struct testsuite_setting *setting =
		hash_table_lookup(settings, identifier);

	if ( setting != NULL ) {
		i_free(setting->identifier);
		i_free(setting->value);
		i_free(setting);

		hash_table_remove(settings, identifier);
	}
}
