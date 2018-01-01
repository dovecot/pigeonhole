/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "strtrim.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-settings.h"
#include "sieve-address.h"
#include "sieve-message.h"

#include "sieve-address-source.h"

bool sieve_address_source_parse
(pool_t pool, const char *value,
	struct sieve_address_source *asrc)
{
	size_t val_len;

	i_zero(asrc);

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
		} else if ( strcmp(value, "user_email") == 0 ) {
			asrc->type = SIEVE_ADDRESS_SOURCE_USER_EMAIL;
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

int sieve_address_source_get_address
(struct sieve_address_source *asrc,
	struct sieve_instance *svinst,
	const struct sieve_script_env *senv,
	struct sieve_message_context *msgctx,
	enum sieve_execute_flags flags,
	const char **addr_r)
{
	enum sieve_address_source_type type = asrc->type;

	if ( type == SIEVE_ADDRESS_SOURCE_USER_EMAIL &&
		svinst->user_email == NULL )
		type = SIEVE_ADDRESS_SOURCE_RECIPIENT;

	if ( (flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) != 0 ) {
		switch ( type ) {
		case SIEVE_ADDRESS_SOURCE_SENDER:
		case SIEVE_ADDRESS_SOURCE_RECIPIENT:
		case SIEVE_ADDRESS_SOURCE_ORIG_RECIPIENT:
			/* We have no envelope */
			type = SIEVE_ADDRESS_SOURCE_DEFAULT;
			break;
		default:
			break;
		}
	}

	switch ( type ) {
	case SIEVE_ADDRESS_SOURCE_SENDER:
		*addr_r = sieve_message_get_sender(msgctx);
		return 1;
	case SIEVE_ADDRESS_SOURCE_RECIPIENT:
		*addr_r = sieve_message_get_final_recipient(msgctx);
		return 1;
	case SIEVE_ADDRESS_SOURCE_ORIG_RECIPIENT:
		*addr_r = sieve_message_get_orig_recipient(msgctx);
		return 1;
	case SIEVE_ADDRESS_SOURCE_USER_EMAIL:
		*addr_r = sieve_address_to_string(svinst->user_email);
		return 1;
	case SIEVE_ADDRESS_SOURCE_POSTMASTER:
		*addr_r = senv->postmaster_address;
		return 1;
	case SIEVE_ADDRESS_SOURCE_EXPLICIT:
		*addr_r = sieve_address_to_string(asrc->address);
		return 1;
	case SIEVE_ADDRESS_SOURCE_DEFAULT:
		break;
	}
	return 0;
}
