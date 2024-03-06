/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "hostpid.h"
#include "var-expand.h"
#include "settings.h"
#include "settings-parser.h"
#include "master-service.h"

#include "sieve.h"

#include "managesieve-settings.h"
#include "managesieve-capabilities.h"

#include <stddef.h>
#include <unistd.h>

/*
 * Global plugin settings
 */


static const char *
plugin_settings_get(const struct plugin_settings *set, const char *identifier)
{
	const char *const *envs;
	unsigned int i, count;

	if ( !array_is_created(&set->plugin_envs) )
		return NULL;

	envs = array_get(&set->plugin_envs, &count);
	for ( i = 0; i < count; i += 2 ) {
		if ( strcmp(envs[i], identifier) == 0 )
			return envs[i+1];
	}
	return NULL;
}

/*
 * Sieve environment
 */

static const char *
sieve_get_setting(struct sieve_instance *svinst ATTR_UNUSED, void *context,
		  const char *identifier)
{
	const struct plugin_settings *set = context;

	return plugin_settings_get(set, identifier);
}

static const struct sieve_callbacks sieve_callbacks = {
	NULL,
	sieve_get_setting,
};

/*
 * Capability dumping
 */

void managesieve_capabilities_dump(void)
{
	const struct plugin_settings *global_plugin_settings;
	struct sieve_environment svenv;
	struct sieve_instance *svinst;
	const char *sieve_cap, *notify_cap;

	/* Read plugin settings */

	global_plugin_settings = settings_get_or_fatal(
		master_service_get_event(master_service),
		&managesieve_plugin_setting_parser_info);

	/* Initialize Sieve engine */

	i_zero(&svenv);
	svenv.home_dir = "/tmp";

	if (sieve_init(&svenv, &sieve_callbacks, (void *)global_plugin_settings,
		       FALSE, &svinst) < 0)
		i_fatal("Failed to initialize Sieve");

	/* Dump capabilities */

	sieve_cap = sieve_get_capabilities(svinst, NULL);
	notify_cap = sieve_get_capabilities(svinst, "notify");

	if (notify_cap == NULL)
		printf("SIEVE: %s\n", sieve_cap);
	else
		printf("SIEVE: %s, NOTIFY: %s\n", sieve_cap, notify_cap);

	settings_free(global_plugin_settings);
	sieve_deinit(&svinst);
}
