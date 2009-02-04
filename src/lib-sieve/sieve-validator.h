/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */
 
#ifndef __SIEVE_VALIDATOR_H
#define __SIEVE_VALIDATOR_H

#include "lib.h"

#include "sieve-common.h"

/*
 * Types
 */

enum sieve_argument_type {
	SAT_NUMBER,
	SAT_CONST_STRING,
	SAT_VAR_STRING,
	SAT_STRING_LIST,
	
	SAT_COUNT
};

struct sieve_command_registration;

/*
 * Validator
 */
 
struct sieve_validator;

struct sieve_validator *sieve_validator_create
	(struct sieve_ast *ast, struct sieve_error_handler *ehandler);
void sieve_validator_free(struct sieve_validator **validator);
pool_t sieve_validator_pool(struct sieve_validator *validator);

bool sieve_validator_run(struct sieve_validator *validator);

/*
 * Accessors
 */
 
struct sieve_error_handler *sieve_validator_error_handler
	(struct sieve_validator *validator);
struct sieve_ast *sieve_validator_ast
	(struct sieve_validator *validator);
struct sieve_script *sieve_validator_script
	(struct sieve_validator *validator);

/*
 * Error handling
 */

void sieve_validator_warning
	(struct sieve_validator *validator, unsigned int source_line, 
		const char *fmt, ...) ATTR_FORMAT(3, 4);
void sieve_validator_error
	(struct sieve_validator *validator, unsigned int source_line, 
		const char *fmt, ...) ATTR_FORMAT(3, 4);
void sieve_validator_critical
	(struct sieve_validator *validator, unsigned int source_line, 
		const char *fmt, ...) ATTR_FORMAT(3, 4);
		
/* 
 * Command/Test registry
 */
 
void sieve_validator_register_command
	(struct sieve_validator *validator, const struct sieve_command *command);
const struct sieve_command *sieve_validator_find_command
	(struct sieve_validator *validator, const char *command);	
	
void sieve_validator_register_external_tag
	(struct sieve_validator *validator, const struct sieve_argument *tag, 
		const char *command, int id_code);

/* 
 * Per-command tagged argument registry
 */

void sieve_validator_register_tag
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg, 
		const struct sieve_argument *argument, int id_code);
void sieve_validator_register_persistent_tag
	(struct sieve_validator *validator, const struct sieve_argument *tag, 
		const char *command);
	
/*
 * Overriding the default literal arguments
 */	
 
void sieve_validator_argument_override
(struct sieve_validator *validator, enum sieve_argument_type type, 
	const struct sieve_argument *argument);
bool sieve_validator_argument_activate_super
(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	struct sieve_ast_argument *arg, bool constant);
		
/* 
 * Argument validation API
 */

bool sieve_validate_positional_argument
	(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *arg, const char *arg_name, unsigned int arg_pos,
	enum sieve_ast_argument_type req_type);
bool sieve_validator_argument_activate
(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *arg, bool constant);
		
bool sieve_validate_tag_parameter
	(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *tag, struct sieve_ast_argument *param,
	enum sieve_ast_argument_type req_type);
	
/* 
 * Extension support
 */

struct sieve_validator_extension {
	const struct sieve_extension *ext;	

	bool (*validate)(struct sieve_validator *valdtr, void *context,
		struct sieve_ast_argument *require_arg);

	void (*free)(struct sieve_validator *valdtr, void *context);
};

const struct sieve_extension *sieve_validator_extension_load
	(struct sieve_validator *validator, struct sieve_ast_argument *ext_arg, 
		string_t *ext_name); 

void sieve_validator_extension_register
	(struct sieve_validator *valdtr, 
		const struct sieve_validator_extension *val_ext, void *context);
void sieve_validator_extension_set_context
(struct sieve_validator *valdtr, const struct sieve_extension *ext, 
	void *context);
void *sieve_validator_extension_get_context
(struct sieve_validator *valdtr, const struct sieve_extension *ext);

/*
 * Validator object registry
 */

struct sieve_validator_object_registry;

struct sieve_validator_object_registry *sieve_validator_object_registry_get
	(struct sieve_validator *validator, const struct sieve_extension *ext);
void sieve_validator_object_registry_add
	(struct sieve_validator_object_registry *regs,
		const struct sieve_object *object);
const struct sieve_object *sieve_validator_object_registry_find
	(struct sieve_validator_object_registry *regs, const char *identifier);
struct sieve_validator_object_registry *sieve_validator_object_registry_create
	(struct sieve_validator *validator);
struct sieve_validator_object_registry *sieve_validator_object_registry_init
	(struct sieve_validator *validator, const struct sieve_extension *ext);

#endif /* __SIEVE_VALIDATOR_H */
