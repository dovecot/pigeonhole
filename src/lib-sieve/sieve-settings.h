/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

#ifndef __SIEVE_SETTINGS_H
#define __SIEVE_SETTINGS_H

#include "sieve-common.h"

struct mail_user;
struct master_service;

void sieve_settings_init
	(struct master_service *service, struct mail_user *user);

/*
 * Retrieval
 */
 
const char *sieve_setting_get(const char *identifier);
const char *sieve_setting_get_ext
	(const struct sieve_extension *ext, const char *identifier);

void sieve_setting_set(const char *identifier, const char *value);
void sieve_setting_set_ext
	(const struct sieve_extension *ext, const char *identifier, const char *value);

#endif /* __SIEVE_SETTINGS_H */
