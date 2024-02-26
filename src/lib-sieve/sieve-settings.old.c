/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-error.h"
#include "sieve-address.h"
#include "sieve-address-source.h"
#include "sieve-settings.old.h"

#include <ctype.h>

/*
 * Access to settings
 */

bool sieve_setting_get_uint_value(struct sieve_instance *svinst,
				  const char *setting,
				  unsigned long long int *value_r)
{
	const char *str_value;

	str_value = sieve_setting_get(svinst, setting);

	if (str_value == NULL || *str_value == '\0')
		return FALSE;

	if (str_to_ullong(str_value, value_r) < 0) {
		e_warning(svinst->event,
			  "invalid unsigned integer value for setting '%s': "
			  "'%s'", setting, str_value);
		return FALSE;
	}
	return TRUE;
}

bool sieve_setting_get_int_value(struct sieve_instance *svinst,
				 const char *setting, long long int *value_r)
{
	const char *str_value;

	str_value = sieve_setting_get(svinst, setting);
	if (str_value == NULL || *str_value == '\0')
		return FALSE;

	if (str_to_llong(str_value, value_r) < 0) {
		e_warning(svinst->event,
			  "invalid integer value for setting '%s': '%s'",
			  setting, str_value);
		return FALSE;
	}
	return TRUE;
}

bool sieve_setting_get_size_value(struct sieve_instance *svinst,
				  const char *setting, size_t *value_r)
{
	const char *str_value;
	uintmax_t value, multiply = 1;
	const char *endp;

	str_value = sieve_setting_get(svinst, setting);
	if (str_value == NULL || *str_value == '\0')
		return FALSE;

	if (str_parse_uintmax(str_value, &value, &endp) < 0) {
		e_warning(svinst->event,
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
		e_warning(svinst->event,
			  "invalid size value for setting '%s': '%s'",
			  setting, str_value);
		return FALSE;
	}

	if (value > SSIZE_T_MAX / multiply) {
		e_warning(svinst->event,
			  "overflowing size value for setting '%s': '%s'",
			  setting, str_value);
		return FALSE;
	}

	*value_r = (size_t)(value * multiply);
	return TRUE;
}

bool sieve_setting_get_bool_value(struct sieve_instance *svinst,
				  const char *setting, bool *value_r)
{
	const char *str_value;

	str_value = sieve_setting_get(svinst, setting);
	if (str_value == NULL)
		return FALSE;

	str_value = t_str_trim(str_value, "\t ");
	if (*str_value == '\0')
		return FALSE;

 	if (strcasecmp(str_value, "yes") == 0) {
		*value_r = TRUE;
		return TRUE;
	}

 	if (strcasecmp(str_value, "no") == 0) {
		*value_r = FALSE;
		return TRUE;
	}

	e_warning(svinst->event,
		  "invalid boolean value for setting '%s': '%s'",
		  setting, str_value);
	return FALSE;
}

bool sieve_setting_get_duration_value(struct sieve_instance *svinst,
				      const char *setting,
				      sieve_number_t *value_r)
{
	const char *str_value;
	uintmax_t value, multiply = 1;
	const char *endp;

	str_value = sieve_setting_get(svinst, setting);
	if (str_value == NULL)
		return FALSE;

	str_value = t_str_trim(str_value, "\t ");
	if (*str_value == '\0')
		return FALSE;

	if (str_parse_uintmax(str_value, &value, &endp) < 0) {
		e_warning(svinst->event,
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
		e_warning(svinst->event,
			  "invalid duration value for setting '%s': '%s'",
			  setting, str_value);
		return FALSE;
	}

	if (value > SIEVE_MAX_NUMBER / multiply) {
		e_warning(svinst->event,
			  "overflowing duration value for setting '%s': '%s'",
			  setting, str_value);
		return FALSE;
	}

	*value_r = (unsigned int)(value * multiply);
	return TRUE;
}

/*
 * Main Sieve engine settings
 */

void sieve_settings_load(struct sieve_instance *svinst)
{
	const char *str_setting, *error;
	unsigned long long int uint_setting;
	size_t size_setting;
	sieve_number_t period;

	svinst->max_script_size = SIEVE_DEFAULT_MAX_SCRIPT_SIZE;
	if (sieve_setting_get_size_value(svinst, "sieve_max_script_size",
					 &size_setting))
		svinst->max_script_size = size_setting;

	svinst->max_actions = SIEVE_DEFAULT_MAX_ACTIONS;
	if (sieve_setting_get_uint_value(svinst, "sieve_max_actions",
					 &uint_setting))
		svinst->max_actions = (unsigned int)uint_setting;

	svinst->max_redirects = SIEVE_DEFAULT_MAX_REDIRECTS;
	if (sieve_setting_get_uint_value(svinst, "sieve_max_redirects",
					 &uint_setting))
		svinst->max_redirects = (unsigned int)uint_setting;

	svinst->max_cpu_time_secs =
		(svinst->env_location == SIEVE_ENV_LOCATION_MS ?
		 0 : SIEVE_DEFAULT_MAX_CPU_TIME_SECS);
	if (sieve_setting_get_duration_value(svinst, "sieve_max_cpu_time",
					     &period)) {
		if (period > (UINT_MAX / 1000))
			svinst->max_cpu_time_secs = (UINT_MAX / 1000);
		else
			svinst->max_cpu_time_secs = (unsigned int)period;
	}
	svinst->resource_usage_timeout_secs =
		SIEVE_DEFAULT_RESOURCE_USAGE_TIMEOUT_SECS;
	if (sieve_setting_get_duration_value(
		svinst, "sieve_resource_usage_timeout", &period)) {
		if (period > UINT_MAX)
			svinst->resource_usage_timeout_secs = UINT_MAX;
		else {
			svinst->resource_usage_timeout_secs =
				(unsigned int)period;
		}
	}

	(void)sieve_address_source_parse_from_setting(
		svinst,	svinst->pool, "sieve_redirect_envelope_from",
		&svinst->redirect_from);

	svinst->redirect_duplicate_period = DEFAULT_REDIRECT_DUPLICATE_PERIOD;
	if (sieve_setting_get_duration_value(
		svinst, "sieve_redirect_duplicate_period", &period)) {
		if (period > UINT_MAX)
			svinst->redirect_duplicate_period = UINT_MAX;
		else {
			svinst->redirect_duplicate_period =
				(unsigned int)period;
		}
	}

	str_setting = sieve_setting_get(svinst, "sieve_user_email");
	if (str_setting != NULL && *str_setting != '\0') {
		struct smtp_address *address;
		if (smtp_address_parse_path(
			svinst->pool, str_setting,
			SMTP_ADDRESS_PARSE_FLAG_BRACKETS_OPTIONAL,
			&address, &error) < 0) {
			e_warning(svinst->event,
				  "Invalid address value for setting "
				  "'sieve_user_email': %s", error);
		} else {
			svinst->user_email = address;
		}
	}
}
