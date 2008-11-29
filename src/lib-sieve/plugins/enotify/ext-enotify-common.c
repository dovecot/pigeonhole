/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */
 
#include "lib.h"
#include "hash.h"

#include "sieve-common.h"

#include "ext-enotify-common.h"

/*
 * Notify capability
 */

static const char *ext_notify_get_methods_string(void);

const struct sieve_extension_capabilities notify_capabilities = {
	"notify",
	ext_notify_get_methods_string
};

/*
 * Notify method registry
 */
 
static struct hash_table *method_index; 

static bool ext_enotify_init(void)
{
	method_index = hash_create
		(default_pool, default_pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
}

static bool ext_enotify_deinit(void)
{
	hash_destroy(&method_index);
}

void sieve_enotify_method_register(const struct sieve_enotify_method *method) 
{
	hash_insert(method_index, (void *) method->identifier, (void *) method);
}

static const char *ext_notify_get_methods_string(void)
{
	return "mailto";
}


