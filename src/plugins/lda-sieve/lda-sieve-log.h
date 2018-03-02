#ifndef LDA_SIEVE_LOG_H
#define LDA_SIEVE_LOG_H

struct sieve_error_handler *lda_sieve_log_ehandler_create
	(struct sieve_error_handler *parent,
		struct mail_deliver_context *mdctx);

#endif
