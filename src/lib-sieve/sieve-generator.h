#ifndef __SIEVE_GENERATOR_H__
#define __SIEVE_GENERATOR_H__

#include "sieve-ast.h"
#include "sieve-code.h"

struct sieve_generator;

struct sieve_generator *sieve_generator_create(struct sieve_ast *ast);
void sieve_generator_free(struct sieve_generator *generator);

void sieve_generator_register_extension
	(struct sieve_generator *generator, const struct sieve_extension *extension);
unsigned int sieve_generator_find_extension
		(struct sieve_generator *generator, const struct sieve_extension *extension); 
		
/* Jump list */

struct sieve_jumplist {
	ARRAY_DEFINE(jumps, sieve_size_t);
};

void sieve_jumplist_init(struct sieve_jumplist *jlist);
void sieve_jumplist_add(struct sieve_jumplist *jlist, sieve_size_t jump);
void sieve_jumplist_resolve(struct sieve_jumplist *jlist, struct sieve_generator *generator);

/* Code emission API */
inline sieve_size_t sieve_generator_emit_data(struct sieve_generator *generator, void *data, sieve_size_t size);
inline sieve_size_t sieve_generator_emit_byte(struct sieve_generator *generator, unsigned char byte); 
inline void sieve_generator_update_data
	(struct sieve_generator *generator, sieve_size_t address, void *data, sieve_size_t size);
inline sieve_size_t sieve_generator_get_current_address(struct sieve_generator *generator);

sieve_size_t sieve_generator_emit_operand
	(struct sieve_generator *generator, int operand);
sieve_size_t sieve_generator_emit_opcode
	(struct sieve_generator *generator, int opcode);
sieve_size_t sieve_generator_emit_ext_opcode
	(struct sieve_generator *generator, const struct sieve_extension *extension);

/* Offset emission */

inline sieve_size_t sieve_generator_emit_offset(struct sieve_generator *generator, int offset);
inline void sieve_generator_resolve_offset(struct sieve_generator *generator, sieve_size_t address); 

/* Literal emission */

inline sieve_size_t sieve_generator_emit_integer
	(struct sieve_generator *generator, sieve_size_t integer);
inline sieve_size_t sieve_generator_emit_string
	(struct sieve_generator *generator, const string_t *str);
	
/* AST generation API */
bool sieve_generate_arguments(struct sieve_generator *generator, 
	struct sieve_command_context *cmd, struct sieve_ast_argument **arg);

bool sieve_generate_block(struct sieve_generator *generator, struct sieve_ast_node *block);
bool sieve_generate_test(struct sieve_generator *generator, struct sieve_ast_node *tst_node, 
	struct sieve_jumplist *jlist, bool jump_true);
struct sieve_binary *sieve_generator_run(struct sieve_generator *genarator);

#endif

