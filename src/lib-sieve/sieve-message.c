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
	pool_t pool;
	struct sieve_message_context *msgctx;
	
	pool = pool_alloconly_create("sieve_message_context", 1024);
	msgctx = p_new(pool, struct sieve_message_context, 1);
	msgctx->pool = pool;
	msgctx->refcount = 1;
	
	p_array_init(&msgctx->ext_contexts, pool, 4);
	
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
	
	*msgctx = NULL;
}

void sieve_message_context_extension_set
(struct sieve_message_context *msgctx, const struct sieve_extension *ext, 
	void *context)
{
	array_idx_set(&msgctx->ext_contexts, (unsigned int) *ext->id, &context);	
}

const void *sieve_message_context_extension_get
(struct sieve_message_context *msgctx, const struct sieve_extension *ext) 
{
	int ext_id = *ext->id;
	void * const *ctx;

	if  ( ext_id < 0 || ext_id >= (int) array_count(&msgctx->ext_contexts) )
		return NULL;
	
	ctx = array_idx(&msgctx->ext_contexts, (unsigned int) ext_id);		

	return *ctx;
}

pool_t sieve_message_context_pool(struct sieve_message_context *msgctx)
{
	return msgctx->pool;
}

