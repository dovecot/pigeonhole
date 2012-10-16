/* Copyright (c) 2002-2012 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "md5.h"
#include "ioloop.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-message.h"
#include "sieve-code.h"
#include "sieve-runtime.h"
#include "sieve-actions.h"

#include "ext-duplicate-common.h"

/*
 * Extension configuration
 */

#define EXT_DUPLICATE_DEFAULT_PERIOD (1*24*60*60)

bool ext_duplicate_load
(const struct sieve_extension *ext, void **context)
{
	struct sieve_instance *svinst = ext->svinst;
	struct ext_duplicate_config *config;
	sieve_number_t default_period, max_period;

	if ( *context != NULL )
		ext_duplicate_unload(ext);

	if ( !sieve_setting_get_duration_value
		(svinst, "sieve_duplicate_default_period", &default_period) ) {
		default_period = EXT_DUPLICATE_DEFAULT_PERIOD;
	}

	if ( !sieve_setting_get_duration_value
		(svinst, "sieve_duplicate_max_period", &max_period) ) {
		max_period = EXT_DUPLICATE_DEFAULT_PERIOD;
	}

	config = i_new(struct ext_duplicate_config, 1);
	config->default_period = default_period;
	config->max_period = max_period;

	*context = (void *) config;
	return TRUE;
}

void ext_duplicate_unload
(const struct sieve_extension *ext)
{
	struct ext_duplicate_config *config =
		(struct ext_duplicate_config *) ext->context;

	i_free(config);
}

/*
 * Duplicate checking
 */

struct ext_duplicate_handle {
	const char *handle;
	unsigned int duplicate:1;
};

struct ext_duplicate_context {
	ARRAY_DEFINE(handles, struct ext_duplicate_handle);

	unsigned int nohandle_duplicate:1;
	unsigned int nohandle_checked:1;
};

bool ext_duplicate_check
(const struct sieve_runtime_env *renv, string_t *handle,
	const char *value, size_t value_len, sieve_number_t period)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	const struct sieve_script_env *senv = renv->scriptenv;
	struct ext_duplicate_context *rctx;
	bool duplicate = FALSE;
	pool_t pool = NULL;
	static const char *id = "sieve duplicate";
	unsigned char dupl_hash[MD5_RESULTLEN];
	struct md5_context ctx;

	if ( !sieve_action_duplicate_check_available(senv)	|| value == NULL )
		return FALSE;

	/* Get context; find out whether duplicate was checked earlier */
	rctx = (struct ext_duplicate_context *)
		sieve_message_context_extension_get(renv->msgctx, this_ext);

	if ( rctx == NULL ) {
		/* Create context */
		pool = sieve_message_context_pool(renv->msgctx);
		rctx = p_new(pool, struct ext_duplicate_context, 1);
		sieve_message_context_extension_set(renv->msgctx, this_ext, (void *)rctx);
	} else {
		if ( handle == NULL ) {
			if ( rctx->nohandle_checked  ) {
				/* Already checked for duplicate */
				return rctx->nohandle_duplicate;
			}
		} else if ( array_is_created(&rctx->handles) ) {
			const struct ext_duplicate_handle *record;
			array_foreach (&rctx->handles, record) {
				if ( strcmp(record->handle, str_c(handle)) == 0 )
					return record->duplicate;
			}
		}
	}		

	/* Create hash */
	md5_init(&ctx);
	md5_update(&ctx, id, strlen(id));
	if (handle != NULL) {
		md5_update(&ctx, "h-", 2);
		md5_update(&ctx, str_c(handle), str_len(handle));
	} else {
		md5_update(&ctx, "default", 7);
	}
	md5_update(&ctx, value, value_len);
	md5_final(&ctx, dupl_hash);

	/* Check duplicate */
	duplicate = sieve_action_duplicate_check
		(senv, dupl_hash, sizeof(dupl_hash));

	/* Create/refresh entry */
	sieve_action_duplicate_mark
		(senv, dupl_hash, sizeof(dupl_hash), ioloop_time + period);

	if ( handle == NULL ) {
		rctx->nohandle_duplicate = duplicate;
		rctx->nohandle_checked = TRUE;
	} else {
		struct ext_duplicate_handle *record;
		
		if ( pool == NULL )
			pool = sieve_message_context_pool(renv->msgctx);
		if ( !array_is_created(&rctx->handles) )
			p_array_init(&rctx->handles, pool, 64);
		record = array_append_space(&rctx->handles);
		record->handle = p_strdup(pool, str_c(handle));
		record->duplicate = duplicate;
	}

	return duplicate;
}

