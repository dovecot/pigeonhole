#ifndef __SIEVE_COMMANDS_H
#define __SIEVE_COMMANDS_H

#include "lib.h"

#include "sieve-common.h"
#include "sieve-ast.h"
#include "sieve-validator.h"
#include "sieve-generator.h"

/* Argument */

struct sieve_argument {
	const char *identifier;
	
	bool (*is_instance_of)(struct sieve_validator *validator, const char *tag);
	
	bool (*validate)
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *context);
	bool (*validate_context)
	(struct sieve_validator *validator, struct sieve_ast_argument *arg, 
		struct sieve_command_context *context);
		
	bool (*generate)(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
		struct sieve_command_context *context);
};

extern const struct sieve_argument number_argument;
extern const struct sieve_argument string_argument;
extern const struct sieve_argument string_list_argument;

/* Command */

enum sieve_command_type {
	SCT_NONE,
	SCT_COMMAND,
	SCT_TEST
};

struct sieve_command {
	const char *identifier;
	enum sieve_command_type type;
	
	/* High-level command syntax */
	int positional_arguments;
	int subtests;
	bool block_allowed;
	bool block_required;
	
	bool (*registered)
		(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg); 
	bool (*pre_validate)
		(struct sieve_validator *validator, struct sieve_command_context *context); 
	bool (*validate)
		(struct sieve_validator *validator, struct sieve_command_context *context); 
	bool (*generate) 
		(struct sieve_generator *generator, struct sieve_command_context *ctx);
	bool (*control_generate) 
		(struct sieve_generator *generator, struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true);
};

struct sieve_command_context {
	const struct sieve_command *command;
	
	/* The registration of this command in the validator */
	struct sieve_command_registration *cmd_reg;

	/* The ast node of this command */
	struct sieve_ast_node *ast_node;
			
	/* First positional argument, found during argument validation */
	struct sieve_ast_argument *first_positional;

	/* The child ast node that unconditionally exits this command's block */
	struct sieve_command_context *block_exit_command;

	/* Command-specific context data*/
	void *data;
};

struct sieve_command_context *sieve_command_context_create
	(struct sieve_ast_node *cmd_node, const struct sieve_command *command);
		
const char *sieve_command_type_name(const struct sieve_command *command);		
		
#define sieve_command_validate_error(validator, context, ...) \
	sieve_validator_error(validator, (context)->ast_node, __VA_ARGS__)
#define sieve_command_validate_critical(validator, context, ...) \
	sieve_validator_critical(validator, (context)->ast_node, __VA_ARGS__)

#define sieve_command_pool(context) \
	sieve_ast_node_pool((context)->ast_node)

#define sieve_command_first_argument(context) \
	sieve_ast_argument_first((context)->ast_node)
	
#define sieve_command_is_toplevel(context) \
	( sieve_ast_node_type(sieve_ast_node_parent((context)->ast_node)) == SAT_ROOT )
#define sieve_command_is_first(context) \
	( sieve_ast_node_prev((context)->ast_node) == NULL )	

inline struct sieve_command_context *sieve_command_prev_context	
	(struct sieve_command_context *context); 
inline struct sieve_command_context *sieve_command_parent_context	
	(struct sieve_command_context *context);
	
inline void sieve_command_exit_block_unconditionally
	(struct sieve_command_context *cmd);
inline bool sieve_command_block_exits_unconditionally
	(struct sieve_command_context *cmd);

#endif /* __SIEVE_COMMANDS_H */
