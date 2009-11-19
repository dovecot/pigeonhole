/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_SETTINGS_H
#define __SIEVE_SETTINGS_H

#include "sieve-common.h"

static inline const char *sieve_get_setting
(struct sieve_instance *svinst, const char *identifier)
{
	const struct sieve_callbacks *callbacks = svinst->callbacks;

	if ( callbacks == NULL || callbacks->get_setting == NULL )
		return NULL;

	return callbacks->get_setting(svinst->context, identifier);
}

bool sieve_get_uint_setting
(struct sieve_instance *svinst, const char *identifier,
	unsigned long long int *value_r);

bool sieve_get_int_setting
(struct sieve_instance *svinst, const char *identifier,
	long long int *value_r);

#endif /* __SIEVE_SETTINGS_H */
