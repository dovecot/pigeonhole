#ifndef __EXT_INCLUDE_COMMON_H
#define __EXT_INCLUDE_COMMON_H

#include "lib.h"
#include "hash.h"

#include "sieve-common.h"

#define EXT_INCLUDE_MAX_NESTING_LEVEL 10

extern int ext_include_my_id;
extern const struct sieve_extension include_extension;

struct ext_include_main_context {
	struct sieve_validator *validator;
	struct hash_table *included_scripts;
};

struct ext_include_validator_context {
	unsigned int nesting_level;
	struct sieve_script *script;
	struct ext_include_main_context *main;
	struct ext_include_validator_context *parent;
};

inline struct ext_include_validator_context *ext_include_get_validator_context
	(struct sieve_validator *validator);
void ext_include_register_validator_context
	(struct sieve_validator *validator, struct sieve_script *script);

bool ext_include_validate_include
	(struct sieve_validator *validator, struct sieve_command_context *cmd,
		const char *script_path, const char *script_name, struct sieve_ast **ast_r);

#endif /* __EXT_INCLUDE_COMMON_H */
