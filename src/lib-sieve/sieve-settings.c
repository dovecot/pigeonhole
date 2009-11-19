/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-settings.h"

#include <stdlib.h>

bool sieve_get_uint_setting
(struct sieve_instance *svinst, const char *identifier,
	unsigned long long int *value_r)
{
	const char *str_value;
	char *endp;

	str_value = sieve_get_setting(svinst, identifier);

	if ( str_value == NULL || *str_value == '\0' )
		return FALSE;

	*value_r = strtoull(str_value, &endp, 10);

	if ( *endp != '\0' )
		return FALSE;
	
	return TRUE;	
}

bool sieve_get_int_setting
(struct sieve_instance *svinst, const char *identifier,
	long long int *value_r)
{
	const char *str_value;
	char *endp;

	str_value = sieve_get_setting(svinst, identifier);

	if ( str_value == NULL || *str_value == '\0' )
		return FALSE;

	*value_r = strtoll(str_value, &endp, 10);

	if ( *endp != '\0' )
		return FALSE;
	
	return TRUE;	
}
