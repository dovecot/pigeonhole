/* Copyright (c) 2002-2015 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "strtrim.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-error.h"
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

bool sieve_setting_get_mail_sender_value
(struct sieve_instance *svinst, pool_t pool, const char *setting,
	struct sieve_mail_sender *sender)
{
	const char *str_value;
	size_t set_len;

	str_value = sieve_setting_get(svinst, setting);
	if ( str_value == NULL )
		return FALSE;

	str_value = ph_t_str_trim(str_value, "\t ");
	str_value = t_str_lcase(str_value);
	set_len = strlen(str_value);
	if ( set_len > 0 ) {
		if ( strcmp(str_value, "default") == 0 ) {
			sender->source = SIEVE_MAIL_SENDER_SOURCE_DEFAULT;
		} else if ( strcmp(str_value, "sender") == 0 ) {
			sender->source = SIEVE_MAIL_SENDER_SOURCE_SENDER;
		} else if ( strcmp(str_value, "recipient") == 0 ) {
			sender->source = SIEVE_MAIL_SENDER_SOURCE_RECIPIENT;
		} else if ( strcmp(str_value, "orig_recipient") == 0 ) {
			sender->source = SIEVE_MAIL_SENDER_SOURCE_ORIG_RECIPIENT;
		} else if ( strcmp(str_value, "postmaster") == 0 ) {
			sender->source = SIEVE_MAIL_SENDER_SOURCE_POSTMASTER;
		} else if ( str_value[0] == '<' &&	str_value[set_len-1] == '>') {
			sender->source = SIEVE_MAIL_SENDER_SOURCE_EXPLICIT;

			str_value = ph_t_str_trim(t_strndup(str_value+1, set_len-2), "\t ");
			sender->address = NULL;
			if ( *str_value != '\0' )
				sender->address = p_strdup(pool, str_value);
		} else {
			sieve_sys_warning(svinst,
				"Invalid value for setting '%s': '%s'", setting,
				str_value);
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * Main Sieve engine settings
 */

void sieve_settings_load
(struct sieve_instance *svinst)
{
	unsigned long long int uint_setting;
	size_t size_setting;

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

	if (!sieve_setting_get_mail_sender_value
		(svinst, svinst->pool, "sieve_redirect_envelope_from",
			&svinst->redirect_from)) {
		svinst->redirect_from.source =
			SIEVE_MAIL_SENDER_SOURCE_DEFAULT;
	}
}


