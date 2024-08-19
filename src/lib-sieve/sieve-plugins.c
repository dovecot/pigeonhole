/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "settings-parser.h"
#include "module-dir.h"
#include "master-service.h"

#include "sieve-extensions.h"

#include "sieve-common.h"
#include "sieve-plugins.h"

/*
 * Types
 */

typedef int
(*sieve_plugin_load_func_t)(struct sieve_instance *svinst, void **context);
typedef void
(*sieve_plugin_unload_func_t)(struct sieve_instance *svinst, void *context);

struct sieve_plugin {
	struct module *module;

	void *context;

	struct sieve_plugin *next;
};

/*
 * Plugin support
 */

static struct module *sieve_modules = NULL;
static int sieve_modules_refcount = 0;

static struct module *sieve_plugin_module_find(const char *name)
{
	struct module *module;

	module = sieve_modules;
	while (module != NULL) {
		const char *mod_name;

		/* Strip module names */
		mod_name = module_get_plugin_name(module);
		if (strcmp(mod_name, name) == 0)
			return module;

		module = module->next;
	}
	return NULL;
}

int sieve_plugins_load(struct sieve_instance *svinst, const char *path,
		       const char *plugins)
{
	struct module *module;
	struct module_dir_load_settings mod_set;
	const char *const *module_names;
	unsigned int i;

	/* Determine what to load */

	if (path == NULL && plugins == NULL) {
		/* From settings */
		module_names = settings_boollist_get(&svinst->set->plugins);
		path = svinst->set->plugin_dir;
	} else {
		/* From function parameters */
		const char **module_names_mod;

		if (plugins == NULL || *plugins == '\0')
			return 0;
		module_names_mod = t_strsplit_spaces(plugins, ", ");

		if (path == NULL || *path == '\0')
			path = sieve_default_settings.plugin_dir;

		for (i = 0; module_names_mod[i] != NULL; i++) {
			/* Allow giving the module names also in non-base form.
			 */
			module_names_mod[i] =
				module_file_get_name(module_names_mod[i]);
		}
		module_names = module_names_mod;
	}

	if (module_names == NULL || *module_names == NULL)
		return 0;

	i_zero(&mod_set);
	mod_set.abi_version = PIGEONHOLE_ABI_VERSION;
	mod_set.binary_name = master_service_get_name(master_service);
	mod_set.setting_name = "sieve_plugins";
	mod_set.require_init_funcs = TRUE;
	mod_set.debug = svinst->debug;

	/* Load missing plugin modules */

	sieve_modules = module_dir_load_missing(sieve_modules, path,
						module_names, &mod_set);

	/* Call plugin load functions for this Sieve instance */

	if (svinst->plugins == NULL)
		sieve_modules_refcount++;

 	for (i = 0; module_names[i] != NULL; i++) {
		struct sieve_plugin *plugin;
		const char *name = module_names[i];
		sieve_plugin_load_func_t load_func;

		/* Find the module */
		module = sieve_plugin_module_find(name);
		i_assert(module != NULL);

		/* Check whether the plugin is already loaded in this instance */
		plugin = svinst->plugins;
		while (plugin != NULL) {
			if (plugin->module == module)
				break;
			plugin = plugin->next;
		}

		/* Skip it if it is loaded already */
		if (plugin != NULL)
			continue;

		/* Create plugin list item */
		plugin = p_new(svinst->pool, struct sieve_plugin, 1);
		plugin->module = module;

		/* Call load function */
		load_func = (sieve_plugin_load_func_t)
			module_get_symbol(module,
				t_strdup_printf("%s_load", module->name));
		if (load_func != NULL &&
		    load_func(svinst, &plugin->context) < 0)
			return -1;

		/* Add plugin to the instance */
		if (svinst->plugins == NULL)
			svinst->plugins = plugin;
		else {
			struct sieve_plugin *plugin_last;

			plugin_last = svinst->plugins;
			while ( plugin_last->next != NULL )
				plugin_last = plugin_last->next;

			plugin_last->next = plugin;
		}
	}
	return 0;
}

void sieve_plugins_unload(struct sieve_instance *svinst)
{
	struct sieve_plugin *plugin;

	if (svinst->plugins == NULL)
		return;

	/* Call plugin unload functions for this instance */

	plugin = svinst->plugins;
	while (plugin != NULL) {
		struct module *module = plugin->module;
		sieve_plugin_unload_func_t unload_func;

		unload_func = (sieve_plugin_unload_func_t)
			module_get_symbol(
				module,
				t_strdup_printf("%s_unload", module->name));
		if (unload_func != NULL)
			unload_func(svinst, plugin->context);

		plugin = plugin->next;
	}

	/* Physically unload modules */

	i_assert(sieve_modules_refcount > 0);

	if (--sieve_modules_refcount != 0)
		return;

	module_dir_unload(&sieve_modules);
}
