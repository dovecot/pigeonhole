#ifndef __SIEVE_COMMANDS_H
#define __SIEVE_COMMANDS_H

#include "lib.h"

#include "sieve-common.h"
#include "sieve-ast.h"

/* 
 * Argument object
 */

struct sieve_argument {
	const char *identifier;
	
	bool (*is_instance_of)
		(struct sieve_validator *validator, struct sieve_command_context *cmdctx,
			struct sieve_ast_argument *arg);
	
	bool (*validate_persistent) // FIXME: this method must be moved down
		(struct sieve_validator *validator, struct sieve_command_context *cmdctx);
	bool (*validate)
		(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
			struct sieve_command_context *context);
	bool (*validate_context)
		(struct sieve_validator *validator, struct sieve_ast_argument *arg, 
			struct sieve_command_context *context);
		
	bool (*generate)
		(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
			struct sieve_command_context *context);
};

/* Literal arguments */

extern const struct sieve_argument number_argument;
extern const struct sieve_argument string_argument;
extern const struct sieve_argument string_list_argument;

/* 
 * Command object
 */

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
		(struct sieve_validator *validator, 
			struct sieve_command_registration *cmd_reg); 
	bool (*pre_validate)
		(struct sieve_validator *validator, struct sieve_command_context *context); 
	bool (*validate)
		(struct sieve_validator *validator, struct sieve_command_context *context); 
	bool (*generate) 
		(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);
	bool (*control_generate) 
		(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true);
};

/*
 * Command context
 */

struct sieve_command_context {
	const struct sieve_command *command;
	
	/* The registration of this command in the validator (sieve-validator.h) */
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
	(struct sieve_ast_node *cmd_node, const struct sieve_command *command,
		struct sieve_command_registration *reg);
		
const char *sieve_command_type_name(const struct sieve_command *command);		

#define sieve_argument_is_string_literal(arg) \
	( (arg)->argument == &string_argument )
		
#define sieve_command_validate_error(validator, context, ...) \
	sieve_validator_error(validator, (context)->ast_node, __VA_ARGS__)
#define sieve_command_validate_warning(validator, context, ...) \
	sieve_validator_warning(validator, (context)->ast_node, __VA_ARGS__)
#define sieve_command_validate_critical(validator, context, ...) \
	sieve_validator_critical(validator, (context)->ast_node, __VA_ARGS__)

#define sieve_command_generate_error(gentr, context, ...) \
	sieve_generator_error(gentr, (context)->ast_node, __VA_ARGS__)
#define sieve_command_generate_critical(gentr, context, ...) \
	sieve_generator_critical(gentr, (context)->ast_node, __VA_ARGS__)

#define sieve_command_pool(context) \
	sieve_ast_node_pool((context)->ast_node)

#define sieve_command_source_line(context) \
	(context)->ast_node->source_line

#define sieve_command_first_argument(context) \
	sieve_ast_argument_first((context)->ast_node)
	
#define sieve_command_is_toplevel(context) \
	( sieve_ast_node_type(sieve_ast_node_parent((context)->ast_node)) == SAT_ROOT )
#define sieve_command_is_first(context) \
	( sieve_ast_node_prev((context)->ast_node) == NULL )	

struct sieve_command_context *sieve_command_prev_context	
	(struct sieve_command_context *context); 
struct sieve_command_context *sieve_command_parent_context	
	(struct sieve_command_context *context);
	
struct sieve_ast_argument *sieve_command_add_dynamic_tag
	(struct sieve_command_context *cmd, const struct sieve_argument *tag,
		int id_code);
struct sieve_ast_argument *sieve_command_find_argument
	(struct sieve_command_context *cmd, const struct sieve_argument *argument);	
	
void sieve_command_exit_block_unconditionally
	(struct sieve_command_context *cmd);
bool sieve_command_block_exits_unconditionally
	(struct sieve_command_context *cmd);
	
/*
 * Core commands
 */
 
extern const struct sieve_command cmd_require;
extern const struct sieve_command cmd_stop;
extern const struct sieve_command cmd_if;
extern const struct sieve_command cmd_elsif;
extern const struct sieve_command cmd_else;
extern const struct sieve_command cmd_redirect;
extern const struct sieve_command cmd_keep;
extern const struct sieve_command cmd_discard;

extern const struct sieve_command *sieve_core_commands[];
extern const unsigned int sieve_core_commands_count;

/* 
 * Core tests 
 */

extern const struct sieve_command tst_true;
extern const struct sieve_command tst_false;
extern const struct sieve_command tst_not;
extern const struct sieve_command tst_anyof;
extern const struct sieve_command tst_allof;
extern const struct sieve_command tst_address;
extern const struct sieve_command tst_header;
extern const struct sieve_command tst_exists;
extern const struct sieve_command tst_size;

extern const struct sieve_command *sieve_core_tests[];
extern const unsigned int sieve_core_tests_count;

#endif /* __SIEVE_COMMANDS_H */
