#ifndef __SIEVE_BINARY_H__
#define __SIEVE_BINARY_H__

#include "sieve-extensions.h"
#include "sieve-code.h"

struct sieve_binary;

struct sieve_binary *sieve_binary_create_new(void);
void sieve_binary_ref(struct sieve_binary *binary);
void sieve_binary_unref(struct sieve_binary **binary);

/* Extension handling */

unsigned int sieve_binary_link_extension(struct sieve_binary *binary, const struct sieve_extension *extension);
const struct sieve_extension *sieve_binary_get_extension(struct sieve_binary *binary, unsigned int index); 

/* Emission functions */

inline sieve_size_t sieve_binary_emit_data(struct sieve_binary *binary, void *data, sieve_size_t size);
inline sieve_size_t sieve_binary_emit_byte(struct sieve_binary *binary, unsigned char byte);
inline void sieve_binary_update_data
	(struct sieve_binary *binary, sieve_size_t address, void *data, sieve_size_t size);
inline sieve_size_t sieve_binary_get_code_size(struct sieve_binary *binary);

/* Retrieval functions */

inline const char *sieve_binary_get_code(struct sieve_binary *binary, sieve_size_t *code_size);

#endif
