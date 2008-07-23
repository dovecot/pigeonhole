#ifndef __SIEVE_MESSAGE_H
#define __SIEVE_MESSAGE_H

/* Message context */

struct sieve_message_context;

struct sieve_message_context *sieve_message_context_create(void);
void sieve_message_context_ref(struct sieve_message_context *msgctx);
void sieve_message_context_unref(struct sieve_message_context **msgctx);

void sieve_message_context_extension_set
	(struct sieve_message_context *msgctx, const struct sieve_extension *ext, 
		void *context);
const void *sieve_message_context_extension_get
	(struct sieve_message_context *msgctx, const struct sieve_extension *ext);
pool_t sieve_message_context_pool
	(struct sieve_message_context *msgctx);
	
#endif
