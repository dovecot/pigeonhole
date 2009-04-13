#include "lib.h"
#include "hash.h"

#include "ext-environment-common.h"

static struct hash_table *environment_items;

/*
 * Core environment items
 */

static const struct sieve_environment_item *core_env_items[] = {
	&domain_env_item, 
	&host_env_item, 
	&location_env_item, 
	&phase_env_item, 
	&name_env_item, 
	&version_env_item
};

static unsigned int core_env_items_count = N_ELEMENTS(core_env_items);

/*
 * Initialization
 */

bool ext_environment_init(void) 
{
	unsigned int i;

	environment_items = hash_table_create
		(default_pool, default_pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);

	for ( i = 0; i < core_env_items_count; i++ ) {
		sieve_ext_environment_item_register(core_env_items[i]);
	}

	return TRUE;
}

void ext_environment_deinit(void)
{
	hash_table_destroy(&environment_items);
}

/*
 * Registration
 */

void sieve_ext_environment_item_register
(const struct sieve_environment_item *item)
{
	hash_table_insert
		(environment_items, (void *) item->name, (void *) item);
}

/*
 * Retrieval
 */

const char *ext_environment_item_get_value
(const char *name, const struct sieve_script_env *senv)
{
	const struct sieve_environment_item *item = 
		(const struct sieve_environment_item *) 
			hash_table_lookup(environment_items, name);

	if ( item == NULL )
		return NULL;

	if ( item->value != NULL )
		return item->value;

	if ( item->get_value != NULL ) 
		return item->get_value(senv);

	return NULL; 
}

/*
 * Default environment items
 */

/* "domain":
 *
 *   The primary DNS domain associated with the Sieve execution context, usually 
 *   but not always a proper suffix of the host name.
 */
const struct sieve_environment_item domain_env_item = {
	"domain",
	NULL,
	NULL,
};

/* "host":
 *
 *   The fully-qualified domain name of the host where the Sieve script is 
 *   executing.
 */

static const char *envit_host_get_value(const struct sieve_script_env *senv)
{
	return senv->hostname != NULL ? senv->hostname : "";
}

const struct sieve_environment_item host_env_item = {
	"host",
	NULL,
	envit_host_get_value,
};

/* "location":
 *
 *   Sieve evaluation can be performed at various different points as messages 
 *   are processed. This item provides additional information about the type of
 *   service that is evaluating the script.  Possible values are:
 *    "MTA" - the Sieve script is being evaluated by a Message Transfer Agent 
 *    "MDA" - evaluation is being performed by a Mail Delivery Agent 
 *    "MUA" - evaluation is being performed by a Mail User Agent
 *    "MS"  - evaluation is being performed by a Message Store
 */
const struct sieve_environment_item location_env_item = {
	"location",
	NULL,
	NULL,
};

/* "phase":
 *
 *   The point relative to final delivery where the Sieve script is being
 *   evaluated.  Possible values are "pre", "during", and "post", referring 
 *   respectively to processing before, during, and after final delivery has 
 *   taken place.
 */

const struct sieve_environment_item phase_env_item = {
	"phase",
	NULL,
	NULL,
};

/* "name":
 *
 *  The product name associated with the Sieve interpreter.
 */
const struct sieve_environment_item name_env_item = {
	"name",
	SIEVE_NAME,
	NULL,
};

/* "version":
 *
 * The product version associated with the Sieve interpreter. The meaning of the 
 * product version string is product-specific and should always be considered
 * in the context of the product name given by the "name" item.
 */

const struct sieve_environment_item version_env_item = {
	"version",
	SIEVE_VERSION,
	NULL,
};





