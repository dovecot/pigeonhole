/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"
#include "str.h"
#include "module-dir.h"

#include "sieve-settings.h"
#include "sieve-extensions.h"

#include "sieve-common.h"
#include "sieve-plugins.h"

struct sieve_plugin {
	struct module *module;
	struct sieve_plugin *next;
};

/*
 * Plugin support
 */

static struct module *sieve_modules = NULL;
static int sieve_modules_refcount = 0;

static struct module *sieve_plugin_module_find(const char *path, const char *name)
{
	struct module *module;

	module = sieve_modules;
    while ( module != NULL ) {
		const char *mod_path, *mod_name;
		char *p;
		size_t len;
		
		/* Strip module paths */

		p = strrchr(module->path, '/');
		if ( p == NULL ) continue;
		while ( p > module->path && *p == '/' ) p--;
		mod_path = t_strdup_until(module->path, p+1);
	
		len = strlen(path);
		if ( path[len-1] == '/' )
			path = t_strndup(path, len-1);

		/* Strip module names */

    	len = strlen(module->name);
    	if (len > 7 && strcmp(module->name + len - 7, "_plugin") == 0)
        	mod_name = t_strndup(module->name, len - 7);
		else
			mod_name = module->name;
		
		if ( strcmp(mod_path, path) == 0 && strcmp(mod_name, name) == 0 )
			return module;

		module = module->next;
    }

    return NULL;
}

void sieve_plugins_load(struct sieve_instance *svinst, const char *path, const char *plugins)
{
	struct module *module;
	const char **module_names;
	string_t *missing_modules;
	unsigned int i;

	/* Determine what to load */

	if ( path == NULL && plugins == NULL ) {
		path = sieve_setting_get(svinst, "sieve_plugin_dir");
		plugins = sieve_setting_get(svinst, "sieve_plugins");
	}

	if ( plugins == NULL || *plugins == '\0' )
		return;
	
	if ( path == NULL || *path == '\0' )
		path = MODULEDIR"/sieve";

	module_names = t_strsplit_spaces(plugins, ", ");

 	for (i = 0; module_names[i] != NULL; i++) {
		/* Allow giving the module names also in non-base form. */
 		module_names[i] = module_file_get_name(module_names[i]);
	}

	/* Load missing modules 
	 *   FIXME: Dovecot should provide this functionality (v2.0 does) 
	 */

	missing_modules = t_str_new(256);

 	for (i = 0; module_names[i] != NULL; i++) {
		const char *name = module_names[i];

		if ( sieve_plugin_module_find(path, name) == NULL ) {
			if ( i > 0 ) str_append_c(missing_modules, ' ');
	
			str_append(missing_modules, name);
		}
	}

	if ( str_len(missing_modules) > 0 ) {
		struct module *new_modules = module_dir_load
			(path, str_c(missing_modules), TRUE, SIEVE_VERSION);

		if ( sieve_modules == NULL ) {
			/* No modules loaded yet */
			sieve_modules = new_modules;
		} else {
			/* Find the end of the list */
			module = sieve_modules;
			while ( module != NULL && module->next != NULL )
				module = module->next;

			/* Add newly loaded modules */
			module->next = new_modules;
		}
	}

	/* Call plugin load functions for this Sieve instance */

	if ( svinst->plugins == NULL ) {
		sieve_modules_refcount++;
	}

 	for (i = 0; module_names[i] != NULL; i++) {
		struct sieve_plugin *plugin;
		const char *name = module_names[i];
		void (*load_func)(struct sieve_instance *svinst);

		/* Find the module */
		module = sieve_plugin_module_find(path, name);
		i_assert(module != NULL);

		/* Check whether the plugin is already loaded in this instance */
		plugin = svinst->plugins;
		while ( plugin != NULL ) {
			if ( plugin->module == module )
				break;
			plugin = plugin->next;
		}

		/* Skip it if it is loaded already */
		if ( plugin != NULL )
			continue;

		/* Create plugin list item */
		plugin = p_new(svinst->pool, struct sieve_plugin, 1);
		plugin->module = module;
	
		/* Call load function */
		load_func = module_get_symbol
			(module, t_strdup_printf("%s_load", module->name));
		if ( load_func != NULL ) {
			load_func(svinst);
		}

		/* Add plugin to the instance */
		if ( svinst->plugins == NULL )
			svinst->plugins = plugin;
		else {
			struct sieve_plugin *plugin_last;

			plugin_last = svinst->plugins;
			while ( plugin_last != NULL )
				plugin_last = plugin_last->next;

			plugin_last->next = plugin;
		}
	}
}

void sieve_plugins_unload(struct sieve_instance *svinst)
{
	struct sieve_plugin *plugin;

	if ( svinst->plugins == NULL )
		return;
	
	/* Call plugin unload functions for this instance */

	plugin = svinst->plugins;
	while ( plugin != NULL ) {
		struct module *module = plugin->module;
		void (*unload_func)(struct sieve_instance *svinst);

		unload_func = module_get_symbol
			(module, t_strdup_printf("%s_unload", module->name));
		if ( unload_func != NULL ) {
			unload_func(svinst);
		}

		plugin = plugin->next;
	}

	/* Physically unload modules */

	i_assert(sieve_modules_refcount > 0);

	if ( --sieve_modules_refcount != 0 )
        return;

	module_dir_unload(&sieve_modules);
}

