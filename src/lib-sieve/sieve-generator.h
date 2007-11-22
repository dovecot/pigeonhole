#ifndef __SIEVE_GENERATOR_H
#define __SIEVE_GENERATOR_H

#include "sieve-ast.h"
#include "sieve-code.h"

struct sieve_generator;

struct sieve_generator *sieve_generator_create(struct sieve_ast *ast);
void sieve_generator_free(struct sieve_generator *generator);

inline void sieve_generator_link_extension
    (struct sieve_generator *generator, int ext_id);
    		
/* Jump list */

struct sieve_jumplist {
	struct sieve_binary *binary;
	ARRAY_DEFINE(jumps, sieve_size_t);
};

void sieve_jumplist_init(struct sieve_jumplist *jlist, struct sieve_binary *sbin);
void sieve_jumplist_add(struct sieve_jumplist *jlist, sieve_size_t jump);
void sieve_jumplist_resolve(struct sieve_jumplist *jlist);

/* Code emission API */
inline sieve_size_t sieve_generator_emit_data(struct sieve_generator *generator, void *data, sieve_size_t size);
inline sieve_size_t sieve_generator_emit_byte(struct sieve_generator *generator, unsigned char byte); 
inline void sieve_generator_update_data
	(struct sieve_generator *generator, sieve_size_t address, void *data, sieve_size_t size);
inline sieve_size_t sieve_generator_get_current_address(struct sieve_generator *generator);

inline struct sieve_binary *sieve_generator_get_binary
	(struct sieve_generator *gentr);
inline sieve_size_t sieve_generator_emit_opcode
	(struct sieve_generator *gentr, const struct sieve_opcode *opcode);
inline sieve_size_t sieve_generator_emit_opcode_ext
	(struct sieve_generator *gentr, const struct sieve_opcode *opcode, int ext_id);

/* Offset emission */

inline sieve_size_t sieve_generator_emit_offset(struct sieve_generator *generator, int offset);
inline void sieve_generator_resolve_offset(struct sieve_generator *generator, sieve_size_t address); 

/* Literal emission */
inline sieve_size_t sieve_generator_emit_byte
	(struct sieve_generator *generator, unsigned char btval);
inline sieve_size_t sieve_generator_emit_integer
	(struct sieve_generator *generator, sieve_size_t integer);
inline sieve_size_t sieve_generator_emit_string
	(struct sieve_generator *generator, const string_t *str);

/* API */

inline void sieve_generator_register_extension
	(struct sieve_generator *gnr, const struct sieve_extension *ext);
bool sieve_generate_arguments(struct sieve_generator *generator, 
	struct sieve_command_context *cmd, struct sieve_ast_argument **arg);

bool sieve_generate_block
	(struct sieve_generator *generator, struct sieve_ast_node *block);
bool sieve_generate_test
	(struct sieve_generator *generator, struct sieve_ast_node *tst_node, 
		struct sieve_jumplist *jlist, bool jump_true);
struct sieve_binary *sieve_generator_run(struct sieve_generator *genarator);

#endif /* __SIEVE_GENERATOR_H */

