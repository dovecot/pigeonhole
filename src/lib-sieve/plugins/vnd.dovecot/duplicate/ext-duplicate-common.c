/* Copyright (c) 2002-2012 Sieve duplicate Plugin authors, see the included
 * COPYING file.
 */

#include "lib.h"
#include "md5.h"
#include "ioloop.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-runtime.h"
#include "sieve-actions.h"
#include "sieve-result.h"

#include "ext-duplicate-common.h"

/*
 * Extension configuration
 */

#define EXT_DUPLICATE_DEFAULT_PERIOD (1*24*60*60)

struct ext_duplicate_config {
	unsigned int period;
};

bool ext_duplicate_load
(const struct sieve_extension *ext, void **context)
{
	struct sieve_instance *svinst = ext->svinst;
	struct ext_duplicate_config *config;
	sieve_number_t period;

	if ( *context != NULL )
		ext_duplicate_unload(ext);

	if ( !sieve_setting_get_duration_value
		(svinst, "sieve_duplicate_period", &period) ) {
		period = EXT_DUPLICATE_DEFAULT_PERIOD;
	}

	config = i_new(struct ext_duplicate_config, 1);
	config->period = period;

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

struct ext_duplicate_context {
	unsigned int duplicate:1;
};

bool ext_duplicate_check
(const struct sieve_runtime_env *renv, string_t *name)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	const struct sieve_script_env *senv = renv->scriptenv;
	struct ext_duplicate_context *rctx;
	pool_t pool;

	/* Get context; find out whether duplicate was checked earlier */
	rctx = (struct ext_duplicate_context *)
		sieve_result_extension_get_context(renv->result, this_ext);

	if ( rctx != NULL ) {
		/* Already checked for duplicate */
		return rctx->duplicate;
	}

	/* Create context */
	pool = sieve_result_pool(renv->result);
	rctx = p_new(pool, struct ext_duplicate_context, 1);
	sieve_result_extension_set_context(renv->result, this_ext, (void *)rctx);

	/* Lookup duplicate */
	if ( sieve_action_duplicate_check_available(senv)
		&& renv->msgdata->id != NULL ) {
		static const char *id = "sieve duplicate";
		struct ext_duplicate_config *ext_config =
			(struct ext_duplicate_config *) this_ext->context;
		unsigned char dupl_hash[MD5_RESULTLEN];
		struct md5_context ctx;

		/* Create hash */
		md5_init(&ctx);
		md5_update(&ctx, id, strlen(id));
		if (name != NULL)
			md5_update(&ctx, str_c(name), str_len(name));
		md5_update(&ctx, renv->msgdata->id, strlen(renv->msgdata->id));
		md5_final(&ctx, dupl_hash);

		/* Check duplicate */
		rctx->duplicate = sieve_action_duplicate_check
			(senv, dupl_hash, sizeof(dupl_hash));

		/* Create/refresh entry */
		sieve_action_duplicate_mark
			(senv, dupl_hash, sizeof(dupl_hash), ioloop_time + ext_config->period);
	}

	return rctx->duplicate;
}

