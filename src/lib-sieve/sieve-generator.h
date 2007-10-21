#ifndef __SIEVE_GENERATOR_H__
#define __SIEVE_GENERATOR_H__

#include "sieve-ast.h"

typedef size_t sieve_size_t;

struct sieve_generator;

struct sieve_generator *sieve_generator_create(struct sieve_ast *ast);
void sieve_generator_free(struct sieve_generator *generator);

/* Jump list */

struct sieve_jumplist {
	array_t ARRAY_DEFINE(jumps, sieve_size_t);
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

sieve_size_t sieve_generator_emit_opcode(struct sieve_generator *generator, const char *extension, int opcode);
sieve_size_t sieve_generator_emit_offset(struct sieve_generator *generator, int offset);
void sieve_generator_resolve_offset(struct sieve_generator *generator, sieve_size_t address); 

sieve_size_t sieve_generator_emit_integer
	(struct sieve_generator *generator, sieve_size_t integer);
sieve_size_t sieve_generator_emit_number
	(struct sieve_generator *generator, sieve_size_t number);
sieve_size_t sieve_generator_emit_string
	(struct sieve_generator *generator, const string_t *str);
sieve_size_t sieve_generator_emit_string_list
	(struct sieve_generator *generator, const struct sieve_ast_argument *strlist);
	
bool sieve_generator_emit_string_argument
	(struct sieve_generator *generator, struct sieve_ast_argument *arg);
bool sieve_generator_emit_stringlist_argument
	(struct sieve_generator *generator, struct sieve_ast_argument *arg);

/* AST generation API */
bool sieve_generate_block(struct sieve_generator *generator, struct sieve_ast_node *block);
bool sieve_generate_test(struct sieve_generator *generator, struct sieve_ast_node *tst_node, 
	struct sieve_jumplist *jlist, bool jump_true);
bool sieve_generate(struct sieve_generator *genarator, buffer_t **code);

#endif

