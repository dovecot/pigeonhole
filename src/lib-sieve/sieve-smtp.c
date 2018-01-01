/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */
#include "lib.h"
#include "smtp-address.h"

#include "sieve-common.h"
#include "sieve-smtp.h"

struct sieve_smtp_context {
	const struct sieve_script_env *senv;
	void *handle;
	
	bool sent:1;
};

bool sieve_smtp_available
(const struct sieve_script_env *senv)
{
	return ( senv->smtp_start != NULL && senv->smtp_add_rcpt != NULL &&
		senv->smtp_send != NULL && senv->smtp_finish != NULL );
}

struct sieve_smtp_context *sieve_smtp_start
(const struct sieve_script_env *senv,
	const struct smtp_address *mail_from)
{
	struct sieve_smtp_context *sctx;
	void *handle;

	if ( !sieve_smtp_available(senv) )
		return NULL;

	handle = senv->smtp_start(senv, mail_from);
	i_assert( handle != NULL );
	
	sctx = i_new(struct sieve_smtp_context, 1);
	sctx->senv = senv;
	sctx->handle = handle;

	return sctx;
}

void sieve_smtp_add_rcpt
(struct sieve_smtp_context *sctx,
	const struct smtp_address *rcpt_to)
{
	i_assert(!sctx->sent);
	sctx->senv->smtp_add_rcpt(sctx->senv, sctx->handle, rcpt_to);
}

struct ostream *sieve_smtp_send
(struct sieve_smtp_context *sctx)
{
	i_assert(!sctx->sent);
	sctx->sent = TRUE;

	return sctx->senv->smtp_send(sctx->senv, sctx->handle);
}

struct sieve_smtp_context *sieve_smtp_start_single
(const struct sieve_script_env *senv,
	const struct smtp_address *rcpt_to,
		const struct smtp_address *mail_from,
	struct ostream **output_r)
{
	struct sieve_smtp_context *sctx;

	sctx = sieve_smtp_start(senv, mail_from);
	sieve_smtp_add_rcpt(sctx, rcpt_to);
	*output_r = sieve_smtp_send(sctx);

	return sctx;
}

void sieve_smtp_abort
(struct sieve_smtp_context *sctx)
{
	const struct sieve_script_env *senv = sctx->senv;
	void *handle = sctx->handle;

	i_free(sctx);
	i_assert(senv->smtp_abort != NULL);
	senv->smtp_abort(senv, handle);
}

int sieve_smtp_finish
(struct sieve_smtp_context *sctx, const char **error_r)
{
	const struct sieve_script_env *senv = sctx->senv;
	void *handle = sctx->handle;

	i_free(sctx);
	return senv->smtp_finish(senv, handle, error_r);
}

