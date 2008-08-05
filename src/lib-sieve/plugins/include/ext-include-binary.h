#ifndef __EXT_INCLUDE_BINARY_H
#define __EXT_INCLUDE_BINARY_H

#include "sieve-common.h"

struct ext_include_binary_context;

struct ext_include_binary_context *ext_include_binary_init
	(struct sieve_binary *sbin, struct sieve_ast *ast);

void ext_include_binary_script_include
	(struct ext_include_binary_context *binctx, struct sieve_script *script,
		enum ext_include_script_location location, unsigned int block_id);
bool ext_include_binary_script_is_included
	(struct ext_include_binary_context *binctx, struct sieve_script *script,
		unsigned int *block_id);
		
bool ext_include_binary_dump(struct sieve_dumptime_env *denv);
		
#endif /* __EXT_INCLUDE_BINARY_H */

