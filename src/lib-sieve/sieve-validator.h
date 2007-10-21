#ifndef __SIEVE_VALIDATOR_H__
#define __SIEVE_VALIDATOR_H__

#include "lib.h"

#include "sieve-common.h"
#include "sieve-error.h"

struct sieve_validator;
struct sieve_command_registration;

struct sieve_validator *sieve_validator_create(struct sieve_ast *ast, struct sieve_error_handler *ehandler);
void sieve_validator_free(struct sieve_validator *validator);

bool sieve_validate(struct sieve_validator *validator);

void sieve_validator_warning
	(struct sieve_validator *validator, struct sieve_ast_node *node, const char *fmt, ...);
void sieve_validator_error
	(struct sieve_validator *validator, struct sieve_ast_node *node, const char *fmt, ...);

/* Command Programmers Interface */

/* Command/Test registration */
void sieve_validator_register_command
	(struct sieve_validator *validator, const struct sieve_command *command);

/* Tag registration */
void sieve_validator_register_tag
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg, 
	const struct sieve_tag *tag);

/* Special test tags */
void sieve_validator_link_comparator_tag
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg); 
void sieve_validator_link_match_type_tags
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg); 
void sieve_validator_link_address_part_tags
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg); 

/* Command validation */
bool sieve_validate_command_arguments
	(struct sieve_validator *validator, struct sieve_command_context *tst, 
	 const unsigned int count, struct sieve_ast_argument **first_positional);
bool sieve_validate_command_subtests
	(struct sieve_validator *validator, struct sieve_command_context *cmd, const unsigned int count);
bool sieve_validate_command_block(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	bool block_allowed, bool block_required);
bool sieve_validator_load_extension
	(struct sieve_validator *validator, struct sieve_command_context *cmd, const char *extension);

#endif /* __SIEVE_VALIDATOR_H__ */
