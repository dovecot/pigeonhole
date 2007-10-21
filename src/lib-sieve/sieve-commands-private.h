#ifndef __SIEVE_COMMANDS_PRIVATE_H__
#define __SIEVE_COMMANDS_PRIVATE_H__

#include "sieve-common.h"
#include "sieve-commands.h"

/* Built-in tests and commands */

/* Stop */
bool cmd_stop_generate(struct sieve_generator *generator, struct sieve_command_context *ctx);
	
/* Keep */
bool cmd_keep_generate(struct sieve_generator *generator, struct sieve_command_context *ctx);

/* Discard */
bool cmd_discard_generate(struct sieve_generator *generator, struct sieve_command_context *ctx);
	
/* False */
bool tst_false_generate(struct sieve_generator *generator, struct sieve_command_context *ctx,
	struct sieve_jumplist *jumps, bool jump_true);

/* True */
bool tst_true_generate(struct sieve_generator *generator, struct sieve_command_context *ctx,
	struct sieve_jumplist *jumps, bool jump_true);

/* Address test */
bool tst_address_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
bool tst_address_validate(struct sieve_validator *validator, struct sieve_command_context *tst); 
bool tst_address_generate(struct sieve_generator *generator, struct sieve_command_context *ctx);

/* Header test */
bool tst_header_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
bool tst_header_validate(struct sieve_validator *validator, struct sieve_command_context *tst); 
bool tst_header_generate(struct sieve_generator *generator, struct sieve_command_context *ctx);

/* Exists test */
bool tst_exists_validate(struct sieve_validator *validator, struct sieve_command_context *tst); 
bool tst_exists_generate(struct sieve_generator *generator, struct sieve_command_context *ctx);

/* Size */
bool tst_size_validate(struct sieve_validator *validator, struct sieve_command_context *tst);
bool tst_size_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
bool tst_size_generate(struct sieve_generator *generator, struct sieve_command_context *ctx);

/* Not test */
bool tst_not_validate(struct sieve_validator *validator, struct sieve_command_context *tst); 
bool tst_not_generate(struct sieve_generator *generator, struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true);

/* Allof test */
bool tst_allof_validate(struct sieve_validator *validator, struct sieve_command_context *tst); 
bool tst_allof_generate(struct sieve_generator *generator, struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true);

/* Anyof test */
bool tst_anyof_validate(struct sieve_validator *validator, struct sieve_command_context *tst); 
bool tst_anyof_generate(struct sieve_generator *generator, struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true);

/* If-elsif-else */
bool cmd_if_validate(struct sieve_validator *validator, struct sieve_command_context *ctx);
bool cmd_elsif_validate(struct sieve_validator *validator, struct sieve_command_context *context);
bool cmd_else_validate(struct sieve_validator *validator, struct sieve_command_context *context);
bool cmd_if_generate(struct sieve_generator *generator, struct sieve_command_context *ctx);
bool cmd_else_generate(struct sieve_generator *generator, struct sieve_command_context *ctx);

bool cmd_redirect_validate(struct sieve_validator *validator, struct sieve_command_context *context);
bool cmd_require_validate(struct sieve_validator *validator, struct sieve_command_context *context);

extern const struct sieve_command sieve_core_commands[];
extern const unsigned int sieve_core_commands_count;

extern const struct sieve_command sieve_core_tests[];
extern const unsigned int sieve_core_tests_count;

#endif /* __SIEVE_COMMANDS_PRIVATE_H__ */

