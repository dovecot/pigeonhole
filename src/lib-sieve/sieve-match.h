#ifndef __SIEVE_MATCH_H
#define __SIEVE_MATCH_H

#include "sieve-common.h"

/*
 * Matching context
 */

struct sieve_match_context {
	struct sieve_interpreter *interp;
	const struct sieve_match_type *match_type;
	const struct sieve_comparator *comparator;
	struct sieve_coded_stringlist *key_list;

	void *data;
};

struct sieve_match_context *sieve_match_begin
	(struct sieve_interpreter *interp, const struct sieve_match_type *mtch, 
		const struct sieve_comparator *cmp, struct sieve_coded_stringlist *key_list);
int sieve_match_value
	(struct sieve_match_context *mctx, const char *value, size_t val_size);
int sieve_match_end(struct sieve_match_context *mctx);

#endif /* __SIEVE_MATCH_H */
