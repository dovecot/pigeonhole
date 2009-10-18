/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"
#include "mail-user.h"
#include "master-service.h"
#include "master-service-settings.h"

#include "sieve-common.h"
#include "sieve-extensions.h"

#include "sieve-settings.h"

#include <stdlib.h>

static struct mail_user *_settings_user; 
static struct master_service *_settings_service;

/*
 * Initialization
 */

void sieve_settings_init
(struct master_service *service, struct mail_user *user) 
{
	_settings_user = user;
	_settings_service = service;
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
	const char *value = mail_user_plugin_getenv
		(_settings_user, _sieve_setting_get_env_name(NULL, identifier));

	printf("GET(%s) = %s\n", identifier, value);

	return value;
}

const char *sieve_setting_get_ext
(const struct sieve_extension *ext, const char *identifier)
{
	return mail_user_plugin_getenv
		(_settings_user, _sieve_setting_get_env_name(ext, identifier));
}

void sieve_setting_set(const char *identifier, const char *value)
{
	const char *setting;

	if ( _settings_service == NULL ) return;

	setting = t_strconcat("plugin/", 
		_sieve_setting_get_env_name(NULL, identifier), "=", value, NULL);
	
	printf("%s\n", setting);

	if ( master_service_set(_settings_service, setting) < 0 )
		i_unreached();
}

void sieve_setting_set_ext
(const struct sieve_extension *ext, const char *identifier, const char *value)
{
	const char *setting;

	if ( _settings_service == NULL ) return;

	setting = t_strconcat("plugin/", 
		_sieve_setting_get_env_name(NULL, identifier), "=", value, NULL);
	
	printf("%s\n", setting);

	if ( master_service_set(_settings_service, setting) < 0 )
		i_unreached();
}
