/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-extensions.h"

#include "sieve-settings.h"

static sieve_settings_func_t sieve_settings_func = NULL;

/*
 * Initialization
 */

void sieve_settings_init(sieve_settings_func_t settings_func)
{
	sieve_settings_func = settings_func;
}

/*
 * Retrieval
 */
 
static const char *_sieve_setting_get_env_name
(const struct sieve_extension *ext, const char *identifier)
{
	if ( ext == NULL )
		return t_str_lcase(t_strconcat("sieve_", identifier, NULL));
	
	return t_str_lcase(t_strconcat("sieve_", ext->name, "_", identifier, NULL));
}

const char *sieve_setting_get(const char *identifier)
{
	if ( sieve_settings_func == NULL )
		return NULL;

	return sieve_settings_func(_sieve_setting_get_env_name(NULL, identifier));
}

const char *sieve_setting_get_ext
(const struct sieve_extension *ext, const char *identifier)
{
	if ( sieve_settings_func == NULL )
		return NULL;

	return sieve_settings_func(_sieve_setting_get_env_name(ext, identifier));
}
