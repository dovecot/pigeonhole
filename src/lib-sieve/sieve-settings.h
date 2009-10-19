/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

#ifndef __SIEVE_SETTINGS_H
#define __SIEVE_SETTINGS_H

#include "sieve-common.h"

typedef const char *(*sieve_settings_func_t)(const char *identifier);

void sieve_settings_init(sieve_settings_func_t settings_func);
 
const char *sieve_setting_get(const char *identifier);
const char *sieve_setting_get_ext
	(const struct sieve_extension *ext, const char *identifier);

#endif /* __SIEVE_SETTINGS_H */
