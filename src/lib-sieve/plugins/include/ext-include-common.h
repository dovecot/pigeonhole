#ifndef __EXT_INCLUDE_COMMON_H
#define __EXT_INCLUDE_COMMON_H

#include "lib.h"
#include "hash.h"

#include "sieve-common.h"

#define EXT_INCLUDE_MAX_NESTING_LEVEL 10

extern int ext_include_my_id;
extern const struct sieve_extension include_extension;

struct ext_include_main_context {
	struct sieve_generator *generator;
	struct hash_table *included_scripts;
};

struct ext_include_generator_context {
	unsigned int nesting_level;
	struct sieve_script *script;
	struct ext_include_main_context *main;
	struct ext_include_generator_context *parent;
};

inline struct ext_include_generator_context *ext_include_get_generator_context
	(struct sieve_generator *gentr);
void ext_include_register_generator_context
	(struct sieve_generator *gentr);

bool ext_include_generate_include
	(struct sieve_generator *gentr, struct sieve_command_context *cmd,
		const char *script_path, const char *script_name);

#endif /* __EXT_INCLUDE_COMMON_H */
