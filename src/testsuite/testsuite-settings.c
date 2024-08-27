/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "settings.h"

#include "sieve-common.h"

#include "testsuite-common.h"
#include "testsuite-settings.h"

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
