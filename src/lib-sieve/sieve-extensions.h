#ifndef __SIEVE_EXTENSIONS_H
#define __SIEVE_EXTENSIONS_H

#include "lib.h"
#include "sieve-common.h"

struct sieve_extension {
	const char *name;
	
	bool (*load)(int ext_id);

	bool (*validator_load)(struct sieve_validator *validator);	
	bool (*generator_load)(struct sieve_generator *generator);

	bool (*binary_load)(struct sieve_binary *binary);
	bool (*interpreter_load)(struct sieve_interpreter *interpreter);

	/* Extension can introduce a single or multiple opcodes */
	union {
		const struct sieve_opcode **list;
		const struct sieve_opcode *single;
	} opcodes;
	unsigned int opcodes_count;

	/* Extension can introduce a single or multiple operands (FIXME) */
	const struct sieve_operand *operand;
};

/* FIXME: This is not ANSI-compliant C, so it might break on some targets.
 * We'll see, otherwise do an ugly typecast to first union element type. 
 */
#define SIEVE_EXT_DEFINE_NO_OBJECTS \
	{ list: NULL }, 0
#define SIEVE_EXT_DEFINE_OBJECT(OBJ) \
	{ single: &OBJ }, 1
#define SIEVE_EXT_DEFINE_OBJECTS(OBJS) \
	{ list: OBJS }, N_ELEMENTS(OBJS)

#define SIEVE_EXT_DEFINE_NO_OPCODES SIEVE_EXT_DEFINE_NO_OBJECTS
#define SIEVE_EXT_DEFINE_OPCODE(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_OPCODES(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

extern const struct sieve_extension *sieve_preloaded_extensions[];
extern const unsigned int sieve_preloaded_extensions_count;

const struct sieve_extension *sieve_extension_acquire(const char *extension);

/* Extensions state */

bool sieve_extensions_init(const char *sieve_plugins ATTR_UNUSED); 
void sieve_extensions_deinit(void);

/* Extension registry */

int sieve_extension_register(const struct sieve_extension *extension);
int sieve_extensions_get_count(void);
const struct sieve_extension *sieve_extension_get_by_id(unsigned int ext_id); 
int sieve_extension_get_by_name(const char *name, const struct sieve_extension **ext); 
int sieve_extension_get_id(const struct sieve_extension *extension); 

#endif /* __SIEVE_EXTENSIONS_H */
