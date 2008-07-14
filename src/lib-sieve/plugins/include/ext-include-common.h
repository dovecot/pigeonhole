#ifndef __EXT_INCLUDE_COMMON_H
#define __EXT_INCLUDE_COMMON_H

#include "lib.h"
#include "hash.h"

#include "sieve-common.h"

/* Configuration */

#define EXT_INCLUDE_MAX_NESTING_LEVEL 10

/* Extension */

extern int ext_include_my_id;
extern const struct sieve_extension include_extension;
extern const struct sieve_binary_extension include_binary_ext;

/* Commands */

extern const struct sieve_command cmd_include;
extern const struct sieve_command cmd_return;
extern const struct sieve_command cmd_import;
extern const struct sieve_command cmd_export;

/* Types */

enum ext_include_opcode {
	EXT_INCLUDE_OPERATION_INCLUDE,
	EXT_INCLUDE_OPERATION_RETURN,
	EXT_INCLUDE_OPERATION_IMPORT,
	EXT_INCLUDE_OPERATION_EXPORT
};

enum ext_include_script_location { 
	EXT_INCLUDE_LOCATION_PERSONAL, 
	EXT_INCLUDE_LOCATION_GLOBAL,
	EXT_INCLUDE_LOCATION_INVALID 
}; 

/* Script access */

const char *ext_include_get_script_path
	(enum ext_include_script_location location, const char *script_name);

/* Generator */

void ext_include_register_generator_context
	(struct sieve_generator *gentr);

bool ext_include_generate_include
	(struct sieve_generator *gentr, struct sieve_command_context *cmd,
		enum ext_include_script_location location, struct sieve_script *script, 
		unsigned *blk_id_r);

/* Interpreter */

void ext_include_runtime_context_init(const struct sieve_runtime_env *renv);

bool ext_include_execute_include
	(const struct sieve_runtime_env *renv, unsigned int block_id);
void ext_include_execute_return(const struct sieve_runtime_env *renv);

#endif /* __EXT_INCLUDE_COMMON_H */
