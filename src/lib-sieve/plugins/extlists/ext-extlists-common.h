#ifndef EXT_EXTLISTS_COMMON_H
#define EXT_EXTLISTS_COMMON_H

#include "lib.h"
#include "str.h"

#include "sieve-common.h"

/*
 * Configuration
 */

struct ext_extlists_cache_entry {
	const char *value;
	bool matched;
};

struct ext_extlists_list {
	const struct ext_extlists_list_settings *set;
	struct dict *dict;

	pool_t cache_pool;
	ARRAY(struct ext_extlists_cache_entry) cache;
};

struct ext_extlists_context {
	const struct ext_extlists_settings *set;

	ARRAY(struct ext_extlists_list) lists;
};

int ext_extlists_load(const struct sieve_extension *ext, void **context_r);
void ext_extlists_unload(const struct sieve_extension *ext);

/*
 * Tagged arguments
 */

extern const struct sieve_argument_def redirect_list_tag;

/*
 * Match types
 */

extern const struct sieve_match_type_def list_match_type;

/*
 * Tests
 */

extern const struct sieve_command_def valid_ext_list_test;

/*
 * Operation
 */

extern const struct sieve_operation_def valid_ext_list_operation;

/*
 * Operand
 */

extern const struct sieve_operand_def list_match_type_operand;

/*
 * Extension
 */

extern const struct sieve_extension_def extlists_extension;
extern const struct sieve_extension_capabilities extlists_capabilities;

/*
 * Runtime
 */

int ext_extlists_runtime_ext_list_validate(const struct sieve_runtime_env *renv,
					   string_t *ext_list_name);

/*
 * Lookup
 */

int ext_extlists_lookup(const struct sieve_runtime_env *renv,
			struct ext_extlists_context *extctx,
			struct sieve_stringlist *value_list,
			struct sieve_stringlist *key_list,
			const char **match_r, bool *found_r);

#endif
