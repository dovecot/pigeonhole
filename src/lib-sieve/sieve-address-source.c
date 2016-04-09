/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "strtrim.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-settings.h"
#include "sieve-address.h"

#include "sieve-address-source.h"

bool sieve_address_source_parse
(pool_t pool, const char *value,
	struct sieve_address_source *asrc)
{
	size_t val_len;

	memset(asrc, 0, sizeof(asrc));

	value = ph_t_str_trim(value, "\t ");
	value = t_str_lcase(value);
	val_len = strlen(value);
	if ( val_len > 0 ) {
		if ( strcmp(value, "default") == 0 ) {
			asrc->type = SIEVE_ADDRESS_SOURCE_DEFAULT;
		} else if ( strcmp(value, "sender") == 0 ) {
			asrc->type = SIEVE_ADDRESS_SOURCE_SENDER;
		} else if ( strcmp(value, "recipient") == 0 ) {
			asrc->type = SIEVE_ADDRESS_SOURCE_RECIPIENT;
		} else if ( strcmp(value, "orig_recipient") == 0 ) {
			asrc->type = SIEVE_ADDRESS_SOURCE_ORIG_RECIPIENT;
		} else if ( strcmp(value, "postmaster") == 0 ) {
			asrc->type = SIEVE_ADDRESS_SOURCE_POSTMASTER;
		} else if ( value[0] == '<' &&	value[val_len-1] == '>') {
			asrc->type = SIEVE_ADDRESS_SOURCE_EXPLICIT;

			asrc->address = sieve_address_parse_envelope_path
				(pool, t_strndup(value+1, val_len-2));
			if (asrc->address == NULL)
				return FALSE;
		} else {
			return FALSE;
		}
	}
	return TRUE;
}

bool sieve_address_source_parse_from_setting
(struct sieve_instance *svinst, pool_t pool,
	const char *setting, struct sieve_address_source *asrc)
{
	const char *value;

	value = sieve_setting_get(svinst, setting);
	if ( value == NULL )
		return FALSE;

	if ( !sieve_address_source_parse(pool, value, asrc) ) {
		sieve_sys_warning(svinst,
			"Invalid value for setting '%s': '%s'",
			setting, value);
		return FALSE;
	}
	return TRUE;
}
