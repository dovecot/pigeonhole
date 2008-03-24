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

/* Main context, currently not used for anything and might be removed */

struct ext_include_main_context {
	struct sieve_generator *generator;
};

/* Generator */

void ext_include_register_generator_context
	(struct sieve_generator *gentr);

bool ext_include_generate_include
	(struct sieve_generator *gentr, struct sieve_command_context *cmd,
		enum ext_include_script_location location, struct sieve_script *script, 
		unsigned *blk_id_r);

/* Binary */

bool ext_include_binary_save(struct sieve_binary *sbin);
bool ext_include_binary_open(struct sieve_binary *sbin);
bool ext_include_binary_up_to_date(struct sieve_binary *sbin);
void ext_include_binary_free(struct sieve_binary *sbin);

/* Interpreter */

void ext_include_register_interpreter_context(struct sieve_interpreter *interp);

bool ext_include_execute_include
	(const struct sieve_runtime_env *renv, unsigned int block_id);
void ext_include_execute_return(const struct sieve_runtime_env *renv);

/* Variables import-export */

void ext_include_import_variable(struct sieve_ast *ast, const char *variable);
bool ext_include_export_variable(struct sieve_ast *ast, const char *variable);

#endif /* __EXT_INCLUDE_COMMON_H */
