#ifndef __SIEVE_EXTENSIONS_H
#define __SIEVE_EXTENSIONS_H

#include "lib.h"
#include "sieve-common.h"

struct sieve_extension {
	const char *name;
	
	bool (*validator_load)(struct sieve_validator *validator);
	bool (*generator_load)(struct sieve_generator *generator);
	bool (*interpreter_load)(struct sieve_interpreter *interpreter);

	const struct sieve_opcode *opcode;
	const struct sieve_operand *operand;
};

const struct sieve_extension *sieve_extension_acquire(const char *extension);

#endif /* __SIEVE_EXTENSIONS_H */
