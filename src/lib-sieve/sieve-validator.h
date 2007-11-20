#ifndef __SIEVE_VALIDATOR_H
#define __SIEVE_VALIDATOR_H

#include "lib.h"

#include "sieve-common.h"
#include "sieve-error.h"

struct sieve_validator;
struct sieve_command_registration;

struct sieve_validator *sieve_validator_create(struct sieve_ast *ast, struct sieve_error_handler *ehandler);
void sieve_validator_free(struct sieve_validator *validator);
inline pool_t sieve_validator_pool(struct sieve_validator *validator);

bool sieve_validator_run(struct sieve_validator *validator);

void sieve_validator_warning
	(struct sieve_validator *validator, struct sieve_ast_node *node, const char *fmt, ...);
void sieve_validator_error
	(struct sieve_validator *validator, struct sieve_ast_node *node, const char *fmt, ...);

/* Command Programmers Interface */

/* Command/Test registration */
void sieve_validator_register_command
	(struct sieve_validator *validator, const struct sieve_command *command);

/* Argument registration */
void sieve_validator_register_tag
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg, 
	const struct sieve_argument *argument, unsigned int id_code);

/* Special test arguments */
void sieve_validator_link_match_type_tags
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg,
		unsigned int id_code); 

/* Argument validation */

bool sieve_validate_positional_argument
	(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *arg, const char *arg_name, unsigned int arg_pos,
	enum sieve_ast_argument_type req_type);
void sieve_validator_argument_activate
	(struct sieve_validator *validator, struct sieve_ast_argument *arg);	 

/* Extensions */
int sieve_validator_extension_load
	(struct sieve_validator *validator, struct sieve_command_context *cmd, 
		const char *ext_name); 
inline void sieve_validator_extension_set_context(struct sieve_validator *validator, int ext_id, void *context);
inline const void *sieve_validator_extension_get_context(struct sieve_validator *validator, int ext_id);

#endif /* __SIEVE_VALIDATOR_H */
