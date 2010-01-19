/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "ioloop.h"
#include "mempool.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-address.h"

#include "sieve-message.h"

/*
 * Message transmission
 */
 
const char *sieve_message_get_new_id
(const struct sieve_script_env *senv)
{
	static int count = 0;
	
	return t_strdup_printf("<dovecot-sieve-%s-%s-%d@%s>",
		dec2str(ioloop_timeval.tv_sec), dec2str(ioloop_timeval.tv_usec),
    count++, senv->hostname);
}

/* 
 * Message context 
 */

struct sieve_message_context {
	pool_t pool;
	int refcount;

	struct sieve_instance *svinst; 

	const struct sieve_message_data *msgdata;

	/* Normalized envelope addresses */

	bool envelope_parsed;

	const struct sieve_address *envelope_sender;
	const struct sieve_address *envelope_recipient;
	
	/* Context data for extensions */
	ARRAY_DEFINE(ext_contexts, void *); 
};

struct sieve_message_context *sieve_message_context_create
(struct sieve_instance *svinst, const struct sieve_message_data *msgdata)
{
	struct sieve_message_context *msgctx;
	
	msgctx = i_new(struct sieve_message_context, 1);
	msgctx->refcount = 1;
	msgctx->svinst = svinst;

	msgctx->msgdata = msgdata;
		
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

	msgctx->envelope_recipient = NULL;
	msgctx->envelope_sender = NULL;
	msgctx->envelope_parsed = FALSE;

	p_array_init(&msgctx->ext_contexts, pool, 
		sieve_extensions_get_count(msgctx->svinst));
}

pool_t sieve_message_context_pool(struct sieve_message_context *msgctx)
{
	return msgctx->pool;
}

/* Extension support */

void sieve_message_context_extension_set
(struct sieve_message_context *msgctx, const struct sieve_extension *ext, 
	void *context)
{
	if ( ext->id < 0 ) return;

	array_idx_set(&msgctx->ext_contexts, (unsigned int) ext->id, &context);	
}

const void *sieve_message_context_extension_get
(struct sieve_message_context *msgctx, const struct sieve_extension *ext) 
{
	void * const *ctx;

	if  ( ext->id < 0 || ext->id >= (int) array_count(&msgctx->ext_contexts) )
		return NULL;
	
	ctx = array_idx(&msgctx->ext_contexts, (unsigned int) ext->id);		

	return *ctx;
}

/* Envelope */

static void sieve_message_envelope_parse(struct sieve_message_context *msgctx)
{
	/* FIXME: log parse problems properly; logs only 'failure' now */

	msgctx->envelope_recipient = sieve_address_parse_envelope_path
		(msgctx->pool, msgctx->msgdata->to_address);	

	if ( msgctx->envelope_recipient == NULL )
		sieve_sys_error("envelope recipient address '%s' is unparsable", msgctx->msgdata->to_address); 
	else if ( msgctx->envelope_recipient->local_part == NULL )
		sieve_sys_error("envelope recipient address '%s' is a null path", msgctx->msgdata->to_address); 

	msgctx->envelope_sender = sieve_address_parse_envelope_path
		(msgctx->pool, msgctx->msgdata->return_path);	

	if ( msgctx->envelope_sender == NULL )
		sieve_sys_error("envelope sender address '%s' is unparsable", msgctx->msgdata->return_path); 

	msgctx->envelope_parsed = TRUE;
}

const struct sieve_address *sieve_message_get_recipient_address
(struct sieve_message_context *msgctx)
{
	if ( !msgctx->envelope_parsed ) 
		sieve_message_envelope_parse(msgctx);

	return msgctx->envelope_recipient;
} 

const struct sieve_address *sieve_message_get_sender_address
(struct sieve_message_context *msgctx)
{
	if ( !msgctx->envelope_parsed ) 
		sieve_message_envelope_parse(msgctx);

	return msgctx->envelope_sender;	
} 

const char *sieve_message_get_recipient
(struct sieve_message_context *msgctx)
{
	if ( !msgctx->envelope_parsed ) 
		sieve_message_envelope_parse(msgctx);

	return sieve_address_to_string(msgctx->envelope_recipient);
}

const char *sieve_message_get_sender
(struct sieve_message_context *msgctx)
{
	if ( !msgctx->envelope_parsed ) 
		sieve_message_envelope_parse(msgctx);

	return sieve_address_to_string(msgctx->envelope_sender);
} 



