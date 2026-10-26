/* Copyright (c) Pigeonhole authors, see top-level COPYING file */
#include "lib.h"
#include "smtp-address.h"

#include "sieve-common.h"
#include "sieve-smtp.h"

struct sieve_smtp_context {
	const struct sieve_script_env *senv;
	void *handle;

	bool sent:1;
};

bool sieve_smtp_available(const struct sieve_script_env *senv)
{
	return (senv->smtp_start != NULL && senv->smtp_add_rcpt != NULL &&
		senv->smtp_send != NULL && senv->smtp_finish != NULL);
}

int sieve_smtp_start(const struct sieve_script_env *senv,
		     const struct smtp_address *mail_from,
		     struct sieve_smtp_context **sctx_r,
		     const char **error_r)
{
	struct sieve_smtp_context *sctx;
	void *handle;

	*sctx_r = NULL;
	*error_r = NULL;

	if (!sieve_smtp_available(senv)) {
		*error_r = "no means to send mail";
		return 0;
	}

	if (senv->smtp_start(senv, mail_from, &handle, error_r) < 0)
		return -1;
	i_assert(handle != NULL);

	sctx = i_new(struct sieve_smtp_context, 1);
	sctx->senv = senv;
	sctx->handle = handle;

	*sctx_r = sctx;
	return 1;
}

void sieve_smtp_add_rcpt(struct sieve_smtp_context *sctx,
			 const struct smtp_address *rcpt_to)
{
	i_assert(!sctx->sent);
	sctx->senv->smtp_add_rcpt(sctx->senv, sctx->handle, rcpt_to);
}

struct ostream *sieve_smtp_send(struct sieve_smtp_context *sctx)
{
	i_assert(!sctx->sent);
	sctx->sent = TRUE;

	return sctx->senv->smtp_send(sctx->senv, sctx->handle);
}

int sieve_smtp_start_single(const struct sieve_script_env *senv,
			    const struct smtp_address *rcpt_to,
			    const struct smtp_address *mail_from,
			    struct ostream **output_r,
			    struct sieve_smtp_context **sctx_r,
			    const char **error_r)
{
	int ret;

	ret = sieve_smtp_start(senv, mail_from, sctx_r, error_r);
	if (ret <= 0)
		return ret;

	sieve_smtp_add_rcpt(*sctx_r, rcpt_to);
	*output_r = sieve_smtp_send(*sctx_r);

	return 1;
}

void sieve_smtp_abort(struct sieve_smtp_context *sctx)
{
	const struct sieve_script_env *senv = sctx->senv;
	void *handle = sctx->handle;

	i_free(sctx);
	i_assert(senv->smtp_abort != NULL);
	senv->smtp_abort(senv, handle);
}

int sieve_smtp_finish(struct sieve_smtp_context *sctx, const char **error_r)
{
	const struct sieve_script_env *senv = sctx->senv;
	void *handle = sctx->handle;

	i_free(sctx);
	return senv->smtp_finish(senv, handle, error_r);
}
