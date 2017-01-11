/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
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

static const char *ATTR_FORMAT(2, 0) lda_sieve_log_expand_message
(struct sieve_error_handler *_ehandler,
	const char *fmt, va_list args)
{
	struct lda_sieve_log_ehandler *ehandler =
		(struct lda_sieve_log_ehandler *) _ehandler;
	struct mail_deliver_context *mdctx = ehandler->mdctx;
	const struct var_expand_table *table;
	string_t *str;

	table = mail_deliver_ctx_get_log_var_expand_table
		(mdctx, t_strdup_vprintf(fmt, args));

	str = t_str_new(256);
	var_expand(str, mdctx->set->deliver_log_format, table);
	return str_c(str);
}

static void ATTR_FORMAT(4, 0) lda_sieve_log_verror
(struct sieve_error_handler *ehandler, unsigned int flags,
	const char *location, const char *fmt, va_list args)
{
	sieve_direct_error(ehandler->svinst,
		ehandler->parent, flags, location, "%s",
		lda_sieve_log_expand_message(ehandler, fmt, args));
}

static void ATTR_FORMAT(4, 0) lda_sieve_log_vwarning
(struct sieve_error_handler *ehandler, unsigned int flags,
	const char *location, const char *fmt, va_list args)
{
	sieve_direct_warning(ehandler->svinst,
		ehandler->parent, flags, location, "%s",
		lda_sieve_log_expand_message(ehandler, fmt, args));
}

static void ATTR_FORMAT(4, 0) lda_sieve_log_vinfo
(struct sieve_error_handler *ehandler, unsigned int flags,
	const char *location, const char *fmt, va_list args)
{
	sieve_direct_info(ehandler->svinst,
		ehandler->parent, flags, location, "%s",
		lda_sieve_log_expand_message(ehandler, fmt, args));
}

static void ATTR_FORMAT(4, 0) lda_sieve_log_vdebug
(struct sieve_error_handler *ehandler, unsigned int flags,
	const char *location, const char *fmt, va_list args)
{
	sieve_direct_debug(ehandler->svinst,
		ehandler->parent, flags, location, "%s",
		lda_sieve_log_expand_message(ehandler, fmt, args));
}

struct sieve_error_handler *lda_sieve_log_ehandler_create
(struct sieve_error_handler *parent,
	struct mail_deliver_context *mdctx)
{
	struct lda_sieve_log_ehandler *ehandler;
	pool_t pool;

	if ( parent == NULL )
		return NULL;

	pool = pool_alloconly_create("lda_sieve_log_ehandler", 2048);
	ehandler = p_new(pool, struct lda_sieve_log_ehandler, 1);
	sieve_error_handler_init_from_parent(&ehandler->handler, pool, parent);

	ehandler->mdctx = mdctx;

	ehandler->handler.verror = lda_sieve_log_verror;
	ehandler->handler.vwarning = lda_sieve_log_vwarning;
	ehandler->handler.vinfo = lda_sieve_log_vinfo;
	ehandler->handler.vdebug = lda_sieve_log_vdebug;

	return &(ehandler->handler);
}

