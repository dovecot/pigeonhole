#ifndef __EXT_RELATIONAL_COMMON_H
#define __EXT_RELATIONAL_COMMON_H

#include "lib.h"
#include "str.h"

#include "sieve-common.h"

/* 
 * Types 
 */

enum ext_relational_match_type {
	RELATIONAL_VALUE,
	RELATIONAL_COUNT
};

enum relational_match {
	REL_MATCH_GREATER,
	REL_MATCH_GREATER_EQUAL,
	REL_MATCH_LESS,
	REL_MATCH_LESS_EQUAL,
	REL_MATCH_EQUAL,
	REL_MATCH_NOT_EQUAL,
	REL_MATCH_INVALID
};

#define REL_MATCH_INDEX(type, match) \
	(type * REL_MATCH_INVALID + match)
#define REL_MATCH_TYPE(index) \
	(index / REL_MATCH_INVALID)
#define REL_MATCH(index) \
	(index % REL_MATCH_INVALID)

/* 
 * Extension definitions 
 */

int ext_relational_my_id;

extern const struct sieve_extension relational_extension;
extern const struct sieve_match_type_extension relational_match_extension;

bool mcht_relational_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_match_type_context *ctx);
bool mcht_value_match
    (struct sieve_match_context *mctx, const char *val, size_t val_size,
        const char *key, size_t key_size, int key_index);

extern const struct sieve_match_type value_match_type;
extern const struct sieve_match_type count_match_type;

extern const struct sieve_match_type rel_match_count_gt;
extern const struct sieve_match_type rel_match_count_ge;
extern const struct sieve_match_type rel_match_count_lt;
extern const struct sieve_match_type rel_match_count_le;
extern const struct sieve_match_type rel_match_count_eq;
extern const struct sieve_match_type rel_match_count_ne;

extern const struct sieve_match_type rel_match_value_gt;
extern const struct sieve_match_type rel_match_value_ge;
extern const struct sieve_match_type rel_match_value_lt;
extern const struct sieve_match_type rel_match_value_le;
extern const struct sieve_match_type rel_match_value_eq;
extern const struct sieve_match_type rel_match_value_ne;

extern const struct sieve_operand rel_match_type_operand;

#endif
