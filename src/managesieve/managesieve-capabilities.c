/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "hostpid.h"
#include "var-expand.h"
#include "settings-parser.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "master-service-settings-cache.h"

#include "sieve.h"

#include "managesieve-capabilities.h"

#include <stddef.h>
#include <unistd.h>

/*
 * Global plugin settings
 */

struct plugin_settings {
	ARRAY_DEFINE(plugin_envs, const char *);
};

static const struct setting_parser_info **plugin_set_roots;

#undef DEF
#define DEF(type, name) \
	{ type, #name, offsetof(struct login_settings, name), NULL }

static const struct setting_define plugin_setting_defines[] = {
	{ SET_STRLIST, "plugin", offsetof(struct plugin_settings, plugin_envs), NULL },

	SETTING_DEFINE_LIST_END
};

static const struct setting_parser_info plugin_setting_parser_info = {
	.module_name = "managesieve",
	.defines = plugin_setting_defines,

	.type_offset = (size_t)-1,
	.struct_size = sizeof(struct plugin_settings),

	.parent_offset = (size_t)-1,
};

static const struct setting_parser_info *default_plugin_set_roots[] = {
	&plugin_setting_parser_info,
	NULL
};

static const struct setting_parser_info **plugin_set_roots = 
	default_plugin_set_roots;

static struct master_service_settings_cache *set_cache;

static struct plugin_settings *
plugin_settings_read(pool_t pool, void ***other_settings_r)
{
	struct master_service_settings_input input;
	const char *error;
	const struct setting_parser_context *parser;
	void *const *cache_sets;
	void **sets;
	unsigned int i, count;

	memset(&input, 0, sizeof(input));
	input.roots = plugin_set_roots;
	input.module = "managesieve";
	input.service = "managesieve";

	if (set_cache == NULL) {
		set_cache = master_service_settings_cache_init
			(master_service, input.module, input.service);
	}

	if (master_service_settings_cache_read(set_cache, &input, NULL,
					       &parser, &error) < 0)
		i_fatal("dump-capability: Error reading configuration: %s", error);

	cache_sets = settings_parser_get_list(parser) + 1;
	for (count = 0; input.roots[count] != NULL; count++) ;
	i_assert(cache_sets[count] == NULL);
	sets = p_new(pool, void *, count + 1);
	for (i = 0; i < count; i++) {
		sets[i] = settings_dup(input.roots[i], cache_sets[i], pool);
		if (!settings_check(input.roots[i], pool, sets[i], &error)) {
			const char *name = input.roots[i]->module_name;
			i_fatal("dump-capability: settings_check(%s) failed: %s",
				name != NULL ? name : "unknown", error);
		}
	}

	*other_settings_r = sets + 1;
	return sets[0];
}

static const char *plugin_settings_get
	(const struct plugin_settings *set, const char *identifier)
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

static void plugin_settings_deinit(void)
{
	if (set_cache != NULL)
		master_service_settings_cache_deinit(&set_cache);
}

/*
 * Sieve environment
 */

static const char *sieve_get_homedir(void *context ATTR_UNUSED)
{
	return "/tmp";
}

static const char *sieve_get_setting
(void *context, const char *identifier)
{
	const struct plugin_settings *set = (const struct plugin_settings *) context;

  return plugin_settings_get(set, identifier);
}

static const struct sieve_environment sieve_env = {
	sieve_get_homedir,
	sieve_get_setting
};

/*
 * Capability dumping
 */

void managesieve_capabilities_dump(void)
{
	pool_t set_pool;
	const struct plugin_settings *global_plugin_settings;
	void **global_other_settings;
	struct sieve_instance *svinst;
	const char *extensions, *notify_cap;
	
	/* Read plugin settings */

	set_pool = pool_alloconly_create("global plugin settings", 4096);

	global_plugin_settings = plugin_settings_read
		(set_pool, &global_other_settings);

	/* Initialize Sieve engine */

	svinst = sieve_init(&sieve_env, (void *) global_plugin_settings);

	extensions = plugin_settings_get(global_plugin_settings, "sieve_extensions");
	if ( extensions != NULL ) {
		sieve_set_extensions(svinst, extensions);
	}

	/* Dump capabilities */

	notify_cap = sieve_get_capabilities(svinst, "notify");

	if ( notify_cap == NULL ) 
		printf("SIEVE: %s\n", sieve_get_capabilities(svinst, NULL));
	else
		printf("SIEVE: %s, NOTIFY: %s\n", sieve_get_capabilities(svinst, NULL),
			sieve_get_capabilities(svinst, "notify"));

	/* Deinitialize */

	plugin_settings_deinit();
}
