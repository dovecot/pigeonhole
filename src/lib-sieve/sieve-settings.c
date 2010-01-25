/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-settings.h"

#include <stdlib.h>
#include <ctype.h>

bool sieve_setting_get_uint_value
(struct sieve_instance *svinst, const char *setting,
	unsigned long long int *value_r)
{
	const char *str_value;
	char *endp;

	str_value = sieve_setting_get(svinst, setting);

	if ( str_value == NULL || *str_value == '\0' )
		return FALSE;

	*value_r = strtoull(str_value, &endp, 10);

	if ( *endp != '\0' ) {
		sieve_sys_warning("invalid unsigned integer value for setting '%s': '%s'",
			setting, str_value);
		return FALSE;
	}
	
	return TRUE;	
}

bool sieve_setting_get_int_value
(struct sieve_instance *svinst, const char *setting,
	long long int *value_r)
{
	const char *str_value;
	char *endp;

	str_value = sieve_setting_get(svinst, setting);

	if ( str_value == NULL || *str_value == '\0' )
		return FALSE;

	*value_r = strtoll(str_value, &endp, 10);

	if ( *endp != '\0' ) {
		sieve_sys_warning("invalid integer value for setting '%s': '%s'",
			setting, str_value);

		return FALSE;
	}
	
	return TRUE;	
}

bool sieve_setting_get_size_value
(struct sieve_instance *svinst, const char *setting,
	size_t *value_r)
{
	const char *str_value;
	unsigned long long int value, multiply = 1;
	char *endp;

	str_value = sieve_setting_get(svinst, setting);

	if ( str_value == NULL || *str_value == '\0' )
		return FALSE;

	value = strtoull(str_value, &endp, 10);

	switch (i_toupper(*endp)) {
	case '\0':
		/* default */
		break;
	case 'B':
		multiply = 1;
		break;
	case 'K':	
		multiply = 1024;
		break;
	case 'M':
		multiply = 1024*1024;
		break;
	case 'G':
		multiply = 1024*1024*1024;
		break;
	case 'T':
		multiply = 1024ULL*1024*1024*1024;
		break;
	default:
		sieve_sys_warning("invalid unsigned integer value for setting '%s': '%s'",
			setting, str_value);
		return FALSE;
	}

	/* FIXME: conversion to size_t may overflow */
	*value_r = (size_t) (value * multiply);
	
	return TRUE;
}

bool sieve_setting_get_bool_value
(struct sieve_instance *svinst, const char *setting,
	bool *value_r)
{
	const char *str_value;

	str_value = sieve_setting_get(svinst, setting);

	if ( str_value == NULL || *str_value == '\0' )
		return FALSE;

 	if ( strcasecmp(str_value, "yes" ) == 0) {
        *value_r = TRUE;
		return TRUE;
	}

 	if ( strcasecmp(str_value, "no" ) == 0) {
        *value_r = FALSE;
		return TRUE;
	}

	sieve_sys_warning("invalid boolean value for setting '%s': '%s'",
		setting, str_value);
	return FALSE;
}
