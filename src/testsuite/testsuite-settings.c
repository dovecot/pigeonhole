/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "settings.h"
#include "mail-user.h"

#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-tool.h"

#include "testsuite-common.h"
#include "testsuite-mailstore.h"
#include "testsuite-settings.h"

static const char *
testsuite_setting_get(struct sieve_instance *svinst, void *context,
		      const char *identifier);

void testsuite_settings_init(void)
{
	sieve_tool_set_setting_callback(sieve_tool,
					testsuite_setting_get, NULL);
}

static const char *
testsuite_setting_get(struct sieve_instance *svinst, void *context ATTR_UNUSED,
		      const char *identifier)
{
	const struct sieve_settings *svset = svinst->set;
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

	user = testsuite_mailstore_get_user();
	if (user == NULL)
		return NULL;
	return mail_user_plugin_getenv(user, identifier);
}

void testsuite_setting_set(const char *identifier, const char *value)
{
	struct sieve_instance *svinst = testsuite_sieve_instance;

	if (svinst == NULL)
		return;

	struct settings_root *set_root;

	set_root = settings_root_find(svinst->event);
	settings_root_override_remove(set_root, identifier,
				      SETTINGS_OVERRIDE_TYPE_CODE);
	settings_root_override(set_root, identifier, value,
			       SETTINGS_OVERRIDE_TYPE_CODE);
}

void testsuite_setting_unset(const char *identifier)
{
	struct sieve_instance *svinst = testsuite_sieve_instance;

	if (svinst == NULL)
		return;

	struct settings_root *set_root;

	set_root = settings_root_find(svinst->event);
	settings_root_override_remove(set_root, identifier,
				      SETTINGS_OVERRIDE_TYPE_CODE);
}
