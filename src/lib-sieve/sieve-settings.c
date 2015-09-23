/* Copyright (c) 2002-2015 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-error.h"
#include "sieve-settings.h"

#include <ctype.h>

// FIXME: add to dovecot
static const char *t_str_trim(const char *str)
{
	const char *p, *pend, *begin;

	p = str;
	pend = str + strlen(str);
	if (p == pend)
		return "";

	while (p < pend && (*p == ' ' || *p == '\t'))
		p++;
	begin = p;

	p = pend - 1;
	while (p > begin && (*p == ' ' || *p == '\t'))
		p--;

	if (p <= begin)
		return "";
	return t_strdup_until(begin, p+1);
}

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

	str_value = t_str_trim(str_value);
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

	str_value = t_str_trim(str_value);
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
	unsigned long long int uint_setting;
	size_t size_setting;
	const char *str_setting;

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

	svinst->redirect_from = SIEVE_REDIRECT_ENVELOPE_FROM_SENDER;
	svinst->redirect_from_explicit = NULL;
	if ( (str_setting=sieve_setting_get
		(svinst, "sieve_redirect_envelope_from")) != NULL ) {
		size_t set_len;

		str_setting = t_str_trim(str_setting);
		str_setting = t_str_lcase(str_setting);
		set_len = strlen(str_setting);
		if ( set_len > 0 ) {
			if ( strcmp(str_setting, "sender") == 0 ) {
				svinst->redirect_from = SIEVE_REDIRECT_ENVELOPE_FROM_SENDER;
			} else if ( strcmp(str_setting, "recipient") == 0 ) {
				svinst->redirect_from = SIEVE_REDIRECT_ENVELOPE_FROM_RECIPIENT;
			} else if ( strcmp(str_setting, "orig_recipient") == 0 ) {
				svinst->redirect_from = SIEVE_REDIRECT_ENVELOPE_FROM_ORIG_RECIPIENT;
			} else if ( str_setting[0] == '<' &&	str_setting[set_len-1] == '>') {
				svinst->redirect_from = SIEVE_REDIRECT_ENVELOPE_FROM_EXPLICIT;

				str_setting = t_str_trim(t_strndup(str_setting+1, set_len-2));
				if ( *str_setting != '\0' ) {
					svinst->redirect_from_explicit =
						p_strdup(svinst->pool, str_setting);
				}
			} else {
				sieve_sys_warning(svinst,
					"Invalid value `%s' for sieve_redirect_envelope_from setting",
					str_setting);
			}
		}
	}
}


