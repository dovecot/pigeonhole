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

static struct hash_table *settings; 

void testsuite_settings_init(void)
{
	settings = hash_table_create
		(default_pool, default_pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
}

void testsuite_settings_deinit(void)
{
	struct hash_iterate_context *itx = 
		hash_table_iterate_init(settings);
	void *key; 
	void *value;
	
	while ( hash_table_iterate(itx, &key, &value) ) {
		struct testsuite_setting *setting = (struct testsuite_setting *) value;

		i_free(setting->identifier);
		i_free(setting->value);
		i_free(setting);
	}

	hash_table_iterate_deinit(&itx); 	

	hash_table_destroy(&settings);
}

const char *testsuite_setting_get
(void *context ATTR_UNUSED, const char *identifier)
{
	struct testsuite_setting *setting = (struct testsuite_setting *) 
		hash_table_lookup(settings, identifier);

	if ( setting == NULL ) {
		return NULL;
	}

	return setting->value;
}

void testsuite_setting_set(const char *identifier, const char *value)
{
	struct testsuite_setting *setting = (struct testsuite_setting *) 
		hash_table_lookup(settings, identifier);

	if ( setting != NULL ) {
		i_free(setting->value);
		setting->value = i_strdup(value);
	} else {
		setting = i_new(struct testsuite_setting, 1);
		setting->identifier = i_strdup(identifier);
		setting->value = i_strdup(value);
	
		hash_table_insert(settings, (void *) identifier, (void *) setting);
	}
}
