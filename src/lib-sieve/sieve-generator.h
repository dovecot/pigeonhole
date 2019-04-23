#ifndef SIEVE_GENERATOR_H
#define SIEVE_GENERATOR_H

#include "sieve-common.h"

/*
 * Code generator
 */

struct sieve_generator;

struct sieve_codegen_env {
	struct sieve_generator *gentr;

	struct sieve_instance *svinst;
	enum sieve_compile_flags flags;

	struct sieve_script *script;
	struct sieve_ast *ast;

	struct sieve_binary *sbin;
	struct sieve_binary_block *sblock;
};

struct sieve_generator *
sieve_generator_create(struct sieve_ast *ast,
		       struct sieve_error_handler *ehandler,
		       enum sieve_compile_flags flags);
void sieve_generator_free(struct sieve_generator **generator);

/*
 * Accessors
 */

struct sieve_error_handler *
sieve_generator_error_handler(struct sieve_generator *gentr);
pool_t sieve_generator_pool(struct sieve_generator *gentr);
struct sieve_script *sieve_generator_script(struct sieve_generator *gentr);
struct sieve_binary *sieve_generator_get_binary(struct sieve_generator *gentr);
struct sieve_binary_block *
sieve_generator_get_block(struct sieve_generator *gentr);

/*
 * Extension support
 */

void sieve_generator_extension_set_context(struct sieve_generator *gentr,
					   const struct sieve_extension *ext,
					   void *context);
const void *
sieve_generator_extension_get_context(struct sieve_generator *gentr,
				      const struct sieve_extension *ext);

/*
 * Jump list
 */

struct sieve_jumplist {
	pool_t pool;
	struct sieve_binary_block *block;
	ARRAY(sieve_size_t) jumps;
};

struct sieve_jumplist *
sieve_jumplist_create(pool_t pool, struct sieve_binary_block *sblock);
void sieve_jumplist_init_temp(struct sieve_jumplist *jlist,
			      struct sieve_binary_block *sblock);
void sieve_jumplist_reset(struct sieve_jumplist *jlist);
void sieve_jumplist_add(struct sieve_jumplist *jlist, sieve_size_t jump);
void sieve_jumplist_resolve(struct sieve_jumplist *jlist);

/*
 * Code generation API
 */

bool sieve_generate_argument(const struct sieve_codegen_env *cgenv,
			     struct sieve_ast_argument *arg,
			     struct sieve_command *cmd);
bool sieve_generate_arguments(const struct sieve_codegen_env *cgenv,
			      struct sieve_command *cmd,
			      struct sieve_ast_argument **last_arg_r);
bool sieve_generate_argument_parameters(const struct sieve_codegen_env *cgenv,
					struct sieve_command *cmd,
					struct sieve_ast_argument *arg);

bool sieve_generate_block(const struct sieve_codegen_env *cgenv,
			  struct sieve_ast_node *block);
bool sieve_generate_test(const struct sieve_codegen_env *cgenv,
			 struct sieve_ast_node *tst_node,
			 struct sieve_jumplist *jlist, bool jump_true);
struct sieve_binary *
sieve_generator_run(struct sieve_generator *gentr,
		    struct sieve_binary_block **sblock_r);

/*
 * Error handling
 */

void sieve_generator_error(struct sieve_generator *gentr,
			   const char *csrc_filename, unsigned int csrc_linenum,
			   unsigned int source_line, const char *fmt, ...)
			   ATTR_FORMAT(5, 6);
#define sieve_generator_error(gentr, ...) \
	sieve_generator_error(gentr, __FILE__, __LINE__, __VA_ARGS__)
void sieve_generator_warning(struct sieve_generator *gentr,
			     const char *csrc_filename,
			     unsigned int csrc_linenum,
			     unsigned int source_line, const char *fmt, ...)
			     ATTR_FORMAT(5, 6);
#define sieve_generator_warning(gentr, ...) \
	sieve_generator_warning(gentr, __FILE__, __LINE__, __VA_ARGS__)
void sieve_generator_critical(struct sieve_generator *gentr,
			      const char *csrc_filename,
			      unsigned int csrc_linenum,
			      unsigned int source_line, const char *fmt, ...)
			      ATTR_FORMAT(5, 6);
#define sieve_generator_critical(gentr, ...) \
	sieve_generator_critical(gentr, __FILE__, __LINE__, __VA_ARGS__)

#endif

