/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "var-expand.h"
#include "lda-settings.h"
#include "mail-deliver.h"

#include "sieve-error-private.h"

#include "lda-sieve-log.h"

struct lda_sieve_log_ehandler {
	struct sieve_error_handler handler;

	struct mail_deliver_context *mdctx;
};

static const char *ATTR_FORMAT(2, 0)
lda_sieve_log_expand_message(struct sieve_error_handler *_ehandler,
			     const char *fmt, va_list args)
{
	struct lda_sieve_log_ehandler *ehandler =
		(struct lda_sieve_log_ehandler *) _ehandler;
	struct mail_deliver_context *mdctx = ehandler->mdctx;
	const struct var_expand_table *table;
	string_t *str;
	const char *error;

	table = mail_deliver_ctx_get_log_var_expand_table(
		mdctx, t_strdup_vprintf(fmt, args));

	str = t_str_new(256);
	if (var_expand(str, mdctx->set->deliver_log_format,
		       table, &error) <= 0) {
		i_error("Failed to expand deliver_log_format=%s: %s",
			mdctx->set->deliver_log_format, error);
	}
	return str_c(str);
}

static void ATTR_FORMAT(4, 0)
lda_sieve_logv(struct sieve_error_handler *ehandler,
	       const struct sieve_error_params *params,
	       enum sieve_error_flags flags, const char *fmt, va_list args)
{
	sieve_direct_log(ehandler->svinst, ehandler->parent,
			 params, flags, "%s",
			 lda_sieve_log_expand_message(ehandler, fmt, args));
}

struct sieve_error_handler *
lda_sieve_log_ehandler_create(struct sieve_error_handler *parent,
			      struct mail_deliver_context *mdctx)
{
	struct lda_sieve_log_ehandler *ehandler;
	pool_t pool;

	if (parent == NULL)
		return NULL;

	pool = pool_alloconly_create("lda_sieve_log_ehandler", 2048);
	ehandler = p_new(pool, struct lda_sieve_log_ehandler, 1);
	sieve_error_handler_init_from_parent(&ehandler->handler, pool, parent);

	ehandler->mdctx = mdctx;
	ehandler->handler.logv = lda_sieve_logv;

	return &(ehandler->handler);
}

