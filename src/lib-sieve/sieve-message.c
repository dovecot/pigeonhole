/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-message.h"
#include "sieve-extensions.h"

/* 
 * Message context 
 */

struct sieve_message_context {
	pool_t pool;
	int refcount;
	
	/* Context data for extensions */
	ARRAY_DEFINE(ext_contexts, void *); 
};

struct sieve_message_context *sieve_message_context_create(void)
{
	struct sieve_message_context *msgctx;
	
	msgctx = i_new(struct sieve_message_context, 1);
	msgctx->refcount = 1;
		
	sieve_message_context_flush(msgctx);

	return msgctx;
}

void sieve_message_context_ref(struct sieve_message_context *msgctx)
{
	msgctx->refcount++;
}

void sieve_message_context_unref(struct sieve_message_context **msgctx)
{
	i_assert((*msgctx)->refcount > 0);

	if (--(*msgctx)->refcount != 0)
		return;
	
	pool_unref(&((*msgctx)->pool));
		
	i_free(*msgctx);
	*msgctx = NULL;
}

void sieve_message_context_flush(struct sieve_message_context *msgctx)
{
	pool_t pool;

	if ( msgctx->pool != NULL ) {
		pool_unref(&msgctx->pool);
	}

	pool = pool_alloconly_create("sieve_message_context", 1024);
	msgctx->pool = pool;

	p_array_init(&msgctx->ext_contexts, pool, sieve_extensions_get_count());
}

pool_t sieve_message_context_pool(struct sieve_message_context *msgctx)
{
	return msgctx->pool;
}

/*
 * Extension support
 */

void sieve_message_context_extension_set
(struct sieve_message_context *msgctx, const struct sieve_extension *ext, 
	void *context)
{
	array_idx_set(&msgctx->ext_contexts, (unsigned int) SIEVE_EXT_ID(ext), &context);	
}

const void *sieve_message_context_extension_get
(struct sieve_message_context *msgctx, const struct sieve_extension *ext) 
{
	int ext_id = SIEVE_EXT_ID(ext);
	void * const *ctx;

	if  ( ext_id < 0 || ext_id >= (int) array_count(&msgctx->ext_contexts) )
		return NULL;
	
	ctx = array_idx(&msgctx->ext_contexts, (unsigned int) ext_id);		

	return *ctx;
}

