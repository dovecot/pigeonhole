#ifndef __EXT_INCLUDE_VARIABLES_H
#define __EXT_INCLUDE_VARIABLES_H

#include "sieve-common.h"
#include "ext-include-common.h"

/*
 * Global variables scope
 */
 
struct ext_include_variables_scope;
 
struct ext_include_variables_scope *ext_include_variables_scope_create
	(pool_t pool);

/* 
 * Variable import-export
 */
 
void ext_include_variable_import(struct sieve_ast *ast, const char *variable);
bool ext_include_variable_export(struct sieve_ast *ast, const char *variable);

#endif /* __EXT_INCLUDE_VARIABLES_H */

