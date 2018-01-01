/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "strtrim.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-error.h"
#include "sieve-address.h"
#include "sieve-address-source.h"
#include "sieve-settings.h"

#include <ctype.h>

/*
 * Access to settings
 */

bool sieve_setting_get_uint_value
(struct sieve_instance *svinst, const char *setting,
	unsigned long long int *value_r)
{
	const char *str_value;

	str_value = sieve_setting_get(svinst, setting);

	if ( str_value == NULL || *str_value == '\0' )
		return FALSE;

	if ( str_to_ullong(str_value, value_r) < 0 ) {
		sieve_sys_warning(svinst,
			"invalid unsigned integer value for setting '%s': '%s'",
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

	str_value = sieve_setting_get(svinst, setting);

	if ( str_value == NULL || *str_value == '\0' )
		return FALSE;

	if ( str_to_llong(str_value, value_r) < 0 ) {
		sieve_sys_warning(svinst,
			"invalid integer value for setting '%s': '%s'",
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
	uintmax_t value, multiply = 1;
	const char *endp;

	str_value = sieve_setting_get(svinst, setting);

	if ( str_value == NULL || *str_value == '\0' )
		return FALSE;

	if ( str_parse_uintmax(str_value, &value, &endp) < 0 ) {
		sieve_sys_warning(svinst,
			"invalid size value for setting '%s': '%s'",
			setting, str_value);
		return FALSE;
	}
	switch (i_toupper(*endp)) {
	case '\0': /* default */
	case 'B': /* byte (useless) */
		multiply = 1;
		break;
	case 'K': /* kilobyte */
		multiply = 1024;
		break;
	case 'M': /* megabyte */
		multiply = 1024*1024;
		break;
	case 'G': /* gigabyte */
		multiply = 1024*1024*1024;
		break;
	case 'T': /* terabyte */
		multiply = 1024ULL*1024*1024*1024;
		break;
	default:
		sieve_sys_warning(svinst,
			"invalid size value for setting '%s': '%s'",
			setting, str_value);
		return FALSE;
	}

	if ( value > SSIZE_T_MAX / multiply ) {
		sieve_sys_warning(svinst,
			"overflowing size value for setting '%s': '%s'",
			setting, str_value);
		return FALSE;
	}

	*value_r = (size_t) (value * multiply);
	return TRUE;
}

bool sieve_setting_get_bool_value
(struct sieve_instance *svinst, const char *setting,
	bool *value_r)
{
	const char *str_value;

	str_value = sieve_setting_get(svinst, setting);
	if ( str_value == NULL )
		return FALSE;

	str_value = ph_t_str_trim(str_value, "\t ");
	if ( *str_value == '\0' )
		return FALSE;

 	if ( strcasecmp(str_value, "yes" ) == 0) {
        *value_r = TRUE;
		return TRUE;
	}

 	if ( strcasecmp(str_value, "no" ) == 0) {
        *value_r = FALSE;
		return TRUE;
	}

	sieve_sys_warning(svinst,
		"invalid boolean value for setting '%s': '%s'",
		setting, str_value);
	return FALSE;
}

bool sieve_setting_get_duration_value
(struct sieve_instance *svinst, const char *setting,
	sieve_number_t *value_r)
{
	const char *str_value;
	uintmax_t value, multiply = 1;
	const char *endp;

	str_value = sieve_setting_get(svinst, setting);
	if ( str_value == NULL )
		return FALSE;

	str_value = ph_t_str_trim(str_value, "\t ");
	if ( *str_value == '\0' )
		return FALSE;

	if ( str_parse_uintmax(str_value, &value, &endp) < 0 ) {
		sieve_sys_warning(svinst,
			"invalid duration value for setting '%s': '%s'",
			setting, str_value);
		return FALSE;
	}
	switch (i_tolower(*endp)) {
	case '\0': /* default */
	case 's': /* seconds */
		multiply = 1;
		break;
	case 'm': /* minutes */
		multiply = 60;
		break;
	case 'h': /* hours */
		multiply = 60*60;
		break;
	case 'd': /* days */
		multiply = 24*60*60;
		break;
	default:
		sieve_sys_warning(svinst,
			"invalid duration value for setting '%s': '%s'",
			setting, str_value);
		return FALSE;
	}

	if ( value > SIEVE_MAX_NUMBER / multiply ) {
		sieve_sys_warning(svinst,
			"overflowing duration value for setting '%s': '%s'",
			setting, str_value);
		return FALSE;
	}

	*value_r = (unsigned int) (value * multiply);
	return TRUE;
}

/*
 * Main Sieve engine settings
 */

void sieve_settings_load
(struct sieve_instance *svinst)
{
	const char *str_setting;
	unsigned long long int uint_setting;
	size_t size_setting;
	sieve_number_t period;

	svinst->max_script_size = SIEVE_DEFAULT_MAX_SCRIPT_SIZE;
	if ( sieve_setting_get_size_value
		(svinst, "sieve_max_script_size", &size_setting) ) {
		svinst->max_script_size = size_setting;
	}

	svinst->max_actions = SIEVE_DEFAULT_MAX_ACTIONS;
	if ( sieve_setting_get_uint_value
		(svinst, "sieve_max_actions", &uint_setting) ) {
		svinst->max_actions = (unsigned int) uint_setting;
	}

	svinst->max_redirects = SIEVE_DEFAULT_MAX_REDIRECTS;
	if ( sieve_setting_get_uint_value
		(svinst, "sieve_max_redirects", &uint_setting) ) {
		svinst->max_redirects = (unsigned int) uint_setting;
	}

	(void)sieve_address_source_parse_from_setting(svinst,
		svinst->pool, "sieve_redirect_envelope_from",
		&svinst->redirect_from);

	svinst->redirect_duplicate_period = DEFAULT_REDIRECT_DUPLICATE_PERIOD;
	if ( sieve_setting_get_duration_value
		(svinst, "sieve_redirect_duplicate_period", &period) ) {
		if (period > UINT_MAX)
			svinst->redirect_duplicate_period = UINT_MAX;
		else
			svinst->redirect_duplicate_period = (unsigned int)period;
	}

	str_setting = sieve_setting_get(svinst, "sieve_user_email");
	if ( str_setting != NULL && *str_setting != '\0' ) {
		svinst->user_email =
			sieve_address_parse_envelope_path
				(svinst->pool, str_setting);
		if ( svinst->user_email == NULL ) {
			sieve_sys_warning(svinst,
				"Invalid address value for setting "
				"`sieve_user_email': `%s'", str_setting);
		}
	}
}


