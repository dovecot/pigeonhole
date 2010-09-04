/* Copyright (c) 2002-2010 Pigeonhole authors, see the included COPYING file
 */

#ifndef __LDA_SIEVE_LOG
#define __LDA_SIEVE_LOG

#include "lib.h"
#include "mail-deliver.h"

struct sieve_error_handler *lda_sieve_log_ehandler_create
	(struct sieve_instance *svinst, struct mail_deliver_context *mdctx,
		unsigned int max_errors);

#endif /* __LDA_SIEVE_LOG */
