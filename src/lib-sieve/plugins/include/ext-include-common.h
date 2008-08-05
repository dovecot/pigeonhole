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

const char *ext_include_get_script_directory
	(enum ext_include_script_location location, const char *script_name);

/* 
 * AST Context 
 */

/* AST context */

struct ext_include_ast_context {
    struct sieve_variable_scope *import_vars;
    struct sieve_variable_scope *global_vars;

    ARRAY_DEFINE(included_scripts, struct sieve_script *);
};

struct ext_include_ast_context *ext_include_create_ast_context
	(struct sieve_ast *ast, struct sieve_ast *parent);
struct ext_include_ast_context *ext_include_get_ast_context
	(struct sieve_ast *ast);

void ext_include_ast_link_included_script
	(struct sieve_ast *ast, struct sieve_script *script);

/* 
 * Generator context
 */

void ext_include_register_generator_context
	(const struct sieve_codegen_env *cgenv);

bool ext_include_generate_include
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *cmd,
		enum ext_include_script_location location, struct sieve_script *script, 
		unsigned *blk_id_r);

/* 
 * Interpreter context
 */

void ext_include_interpreter_context_init(struct sieve_interpreter *interp);

bool ext_include_execute_include
	(const struct sieve_runtime_env *renv, unsigned int block_id);
void ext_include_execute_return(const struct sieve_runtime_env *renv);

#endif /* __EXT_INCLUDE_COMMON_H */
