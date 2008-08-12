#ifndef __EXT_INCLUDE_BINARY_H
#define __EXT_INCLUDE_BINARY_H

#include "sieve-common.h"

/*
 * Binary context management
 */

struct ext_include_binary_context;

struct ext_include_binary_context *ext_include_binary_init
	(struct sieve_binary *sbin, struct sieve_ast *ast);
struct ext_include_binary_context *ext_include_binary_get_context
	(struct sieve_binary *sbin);

/*
 * Variables
 */

struct sieve_variable_scope *ext_include_binary_get_global_scope
    (struct sieve_binary *sbin);

/*
 * Including scripts
 */

struct ext_include_script_info {
    unsigned int id;

    struct sieve_script *script;
    enum ext_include_script_location location;

    unsigned int block_id;
};

const struct ext_include_script_info *ext_include_binary_script_include
	(struct ext_include_binary_context *binctx, struct sieve_script *script,
		enum ext_include_script_location location, unsigned int block_id);
bool ext_include_binary_script_is_included
	(struct ext_include_binary_context *binctx, struct sieve_script *script,
		const struct ext_include_script_info **script_info_r);

const struct ext_include_script_info *ext_include_binary_script_get_included
	(struct ext_include_binary_context *binctx, unsigned int include_id);
const struct ext_include_script_info *ext_include_binary_script_get
	(struct ext_include_binary_context *binctx, struct sieve_script *script);
unsigned int ext_include_binary_script_get_count
	(struct ext_include_binary_context *binctx);

bool ext_include_binary_dump(struct sieve_dumptime_env *denv);
		
#endif /* __EXT_INCLUDE_BINARY_H */

