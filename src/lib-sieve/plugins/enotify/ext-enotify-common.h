/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __EXT_ENOTIFY_COMMON_H
#define __EXT_ENOTIFY_COMMON_H

#include "sieve-ext-variables.h"

#include "sieve-ext-enotify.h"

/*
 * Extension
 */

extern const struct sieve_extension enotify_extension;
extern const struct sieve_extension_capabilities notify_capabilities;

/*
 * Commands
 */

extern const struct sieve_command notify_command;

/*
 * Tests
 */

extern const struct sieve_command valid_notify_method_test;
extern const struct sieve_command notify_method_capability_test;

/*
 * Operations
 */

extern const struct sieve_operation notify_operation;
extern const struct sieve_operation valid_notify_method_operation;
extern const struct sieve_operation notify_method_capability_operation;

enum ext_variables_opcode {
	EXT_ENOTIFY_OPERATION_NOTIFY,
	EXT_ENOTIFY_OPERATION_VALID_NOTIFY_METHOD,
	EXT_ENOTIFY_OPERATION_NOTIFY_METHOD_CAPABILITY
};

/*
 * Operands
 */
 
extern const struct sieve_operand encodeurl_operand;

/*
 * Modifiers
 */

extern const struct sieve_variables_modifier encodeurl_modifier;

/*
 * Notify methods
 */
 
extern const struct sieve_enotify_method mailto_notify;
 
void ext_enotify_methods_init(void);
void ext_enotify_methods_deinit(void);

const struct sieve_enotify_method *ext_enotify_method_find
	(const char *identifier);
	
/*
 * Validation
 */
 
bool ext_enotify_compile_check_arguments
(struct sieve_validator *valdtr, struct sieve_ast_argument *uri_arg,
	struct sieve_ast_argument *msg_arg, struct sieve_ast_argument *from_arg);

/*
 * Runtime
 */

const struct sieve_enotify_method *ext_enotify_runtime_check_operands
	(const struct sieve_runtime_env *renv, unsigned int source_line,
		string_t *method_uri, string_t *message, string_t *from, 
		void **context);

bool ext_enotify_runtime_method_validate
	(const struct sieve_runtime_env *renv, unsigned int source_line,
		string_t *method_uri);

/*
 * Method logging
 */ 

struct sieve_enotify_log {
	struct sieve_error_handler *ehandler;
	
	const char *location;
	const char *prefix;
};

#endif /* __EXT_ENOTIFY_COMMON_H */
