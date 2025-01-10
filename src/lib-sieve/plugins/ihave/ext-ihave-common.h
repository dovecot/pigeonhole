#ifndef EXT_IHAVE_COMMON_H
#define EXT_IHAVE_COMMON_H

#include "sieve-common.h"

/*
 * Extensions
 */

extern const struct sieve_extension_def ihave_extension;

/*
 * Tests
 */

extern const struct sieve_command_def ihave_test;

/*
 * Commands
 */

extern const struct sieve_command_def error_command;

/*
 * Operations
 */

enum ext_ihave_opcode {
	EXT_IHAVE_OPERATION_IHAVE,
	EXT_IHAVE_OPERATION_ERROR
};

extern const struct sieve_operation_def tst_ihave_operation;
extern const struct sieve_operation_def cmd_error_operation;

/*
 * AST context
 */

struct ext_ihave_ast_context {
	ARRAY(const char *) missing_extensions;
};

struct ext_ihave_ast_context *
ext_ihave_get_ast_context(const struct sieve_extension *this_ext,
			  struct sieve_ast *ast);

void ext_ihave_ast_add_missing_extension(const struct sieve_extension *this_ext,
					 struct sieve_ast *ast,
					 const char *ext_name);

#endif
