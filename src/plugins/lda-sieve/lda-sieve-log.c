/* Copyright (c) 2002-2010 Pigeonhole authors, see the included COPYING file
 */
 
#include "lib.h"
#include "str.h"
#include "ostream.h"
#include "mail-deliver.h"

#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-error-private.h"

#include "lda-sieve-log.h"

/* 
 * Deliver log error handler
 */

struct lda_sieve_log_ehandler {
	struct sieve_error_handler handler;

	struct mail_deliver_context *mdctx;
};

typedef void (*log_func_t)(const char *fmt, ...) ATTR_FORMAT(1, 2);

static void lda_sieve_vlog
(struct sieve_error_handler *_ehandler, log_func_t log_func,
	const char *location, const char *fmt, va_list args) 
{
	struct lda_sieve_log_ehandler *ehandler =
		(struct lda_sieve_log_ehandler *) _ehandler;
	struct mail_deliver_context *mdctx = ehandler->mdctx;
	string_t *str;

	if ( _ehandler->log_master ) return;

	str = t_str_new(256);
	if ( mdctx->session_id != NULL)
		str_printfa(str, "%s: ", mdctx->session_id);

	str_append(str, "sieve: ");

	if ( location != NULL && *location != '\0' )
		str_printfa(str, "%s: ", location);

	str_vprintfa(str, fmt, args);

	log_func("%s", str_c(str));
}

static void lda_sieve_log_verror
(struct sieve_error_handler *ehandler, const char *location,
	const char *fmt, va_list args) 
{
	lda_sieve_vlog(ehandler, i_error, location, fmt, args);
}
static void lda_sieve_log_vwarning
(struct sieve_error_handler *ehandler, const char *location,
	const char *fmt, va_list args) 
{
	lda_sieve_vlog(ehandler, i_warning, location, fmt, args);
}
static void lda_sieve_log_vinfo
(struct sieve_error_handler *ehandler, const char *location,
	const char *fmt, va_list args) 
{
	lda_sieve_vlog(ehandler, i_info, location, fmt, args);
}

static void lda_sieve_log_vdebug
(struct sieve_error_handler *ehandler, const char *location,
	const char *fmt, va_list args) 
{
	lda_sieve_vlog(ehandler, i_debug, location, fmt, args);
}

struct sieve_error_handler *lda_sieve_log_ehandler_create
(struct sieve_instance *svinst, struct mail_deliver_context *mdctx,
	unsigned int max_errors)
{
	pool_t pool;
	struct lda_sieve_log_ehandler *ehandler;

	pool = pool_alloconly_create("lda_sieve_log_error_handler", 256);
	ehandler = p_new(pool, struct lda_sieve_log_ehandler, 1);
	ehandler->mdctx = mdctx;

	sieve_error_handler_init(&ehandler->handler, svinst, pool, max_errors);

	ehandler->handler.verror = lda_sieve_log_verror;
	ehandler->handler.vwarning = lda_sieve_log_vwarning;
	ehandler->handler.vinfo = lda_sieve_log_vinfo;
	ehandler->handler.vdebug = lda_sieve_log_vdebug;

	return &(ehandler->handler);
}
