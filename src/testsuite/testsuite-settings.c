/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "hash.h"
#include "imem.h"
#include "strfuncs.h"
#include "settings.h"
#include "mail-user.h"

#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-tool.h"

#include "testsuite-common.h"
#include "testsuite-mailstore.h"
#include "testsuite-settings.h"

struct testsuite_setting {
	char *identifier;
	char *value;
};

static HASH_TABLE(const char *, struct testsuite_setting *) settings;

static const char *
testsuite_setting_get(struct sieve_instance *svinst, void *context,
		      const char *identifier);

void testsuite_settings_init(void)
{
	hash_table_create(&settings, default_pool, 0, str_hash, strcmp);

	sieve_tool_set_setting_callback(sieve_tool,
					testsuite_setting_get, NULL);
}

void testsuite_settings_deinit(void)
{
	struct hash_iterate_context *itx =
		hash_table_iterate_init(settings);
	const char *key;
	struct testsuite_setting *setting;

	while (hash_table_iterate(itx, settings, &key, &setting)) {
		i_free(setting->identifier);
		i_free(setting->value);
		i_free(setting);
	}

	hash_table_iterate_deinit(&itx);

	hash_table_destroy(&settings);
}

static const char *
testsuite_setting_get(struct sieve_instance *svinst, void *context ATTR_UNUSED,
		      const char *identifier)
{
	const struct sieve_settings *svset = svinst->set;
	struct testsuite_setting *setting;
	struct mail_user *user;

	if (strcmp(identifier, "sieve_max_script_size") == 0)
		return t_strdup_printf("%zu", svset->max_script_size);
	else if (strcmp(identifier, "sieve_max_actions") == 0)
		return t_strdup_printf("%u", svset->max_actions);
	else if (strcmp(identifier, "sieve_max_redirects") == 0)
		return t_strdup_printf("%u", svset->max_redirects);
	else if (strcmp(identifier, "sieve_max_cpu_time") == 0)
		return t_strdup_printf("%u", svset->max_cpu_time);
	else if (strcmp(identifier, "sieve_resource_usage_timeout") == 0)
		return t_strdup_printf("%u", svset->resource_usage_timeout);
	else if (strcmp(identifier, "sieve_redirect_envelope_from") == 0)
		return svset->redirect_envelope_from;
	else if (strcmp(identifier, "sieve_redirect_duplicate_period") == 0)
		return t_strdup_printf("%u", svset->redirect_duplicate_period);
	else if (strcmp(identifier, "sieve_user_email") == 0)
		return svset->user_email;

	setting = hash_table_lookup(settings, identifier);
	if (setting != NULL)
		return setting->value;

	user = testsuite_mailstore_get_user();
	if (user == NULL)
		return NULL;
	return mail_user_plugin_getenv(user, identifier);
}

void testsuite_setting_set(const char *identifier, const char *value)
{
	struct sieve_instance *svinst = testsuite_sieve_instance;

	if (svinst != NULL) {
		struct settings_root *set_root;

		set_root = settings_root_find(svinst->event);
		settings_root_override_remove(set_root, identifier,
					      SETTINGS_OVERRIDE_TYPE_CODE);
		settings_root_override(set_root, identifier, value,
				       SETTINGS_OVERRIDE_TYPE_CODE);
	}

	struct testsuite_setting *setting =
		hash_table_lookup(settings, identifier);

	if (setting != NULL) {
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
	struct sieve_instance *svinst = testsuite_sieve_instance;

	if (svinst != NULL) {
		struct settings_root *set_root;

		set_root = settings_root_find(svinst->event);
		settings_root_override_remove(set_root, identifier,
					      SETTINGS_OVERRIDE_TYPE_CODE);
	}

	struct testsuite_setting *setting =
		hash_table_lookup(settings, identifier);

	if (setting != NULL) {
		i_free(setting->identifier);
		i_free(setting->value);
		i_free(setting);

		hash_table_remove(settings, identifier);
	}
}
