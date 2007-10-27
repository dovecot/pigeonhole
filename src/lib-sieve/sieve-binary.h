#ifndef __SIEVE_BINARY_H
#define __SIEVE_BINARY_H

#include "lib.h"
#include "str.h"

#include "sieve-code.h"
#include "sieve-extensions.h"

struct sieve_binary;

struct sieve_binary *sieve_binary_create_new(void);
void sieve_binary_ref(struct sieve_binary *binary);
void sieve_binary_unref(struct sieve_binary **binary);

/* 
 * Extension handling 
 */

unsigned int sieve_binary_link_extension(struct sieve_binary *binary, const struct sieve_extension *extension);
const struct sieve_extension *sieve_binary_get_extension(struct sieve_binary *binary, unsigned int index); 

/* 
 * Code emission 
 */
 
/* Low-level emission functions */

inline sieve_size_t sieve_binary_emit_data(struct sieve_binary *binary, void *data, sieve_size_t size);
inline sieve_size_t sieve_binary_emit_byte(struct sieve_binary *binary, unsigned char byte);
inline void sieve_binary_update_data
	(struct sieve_binary *binary, sieve_size_t address, void *data, sieve_size_t size);
inline sieve_size_t sieve_binary_get_code_size(struct sieve_binary *binary);

/* Offset emission functions */

sieve_size_t sieve_binary_emit_offset(struct sieve_binary *binary, int offset);
void sieve_binary_resolve_offset(struct sieve_binary *binary, sieve_size_t address);

/* Literal emission functions */

sieve_size_t sieve_binary_emit_integer(struct sieve_binary *binary, sieve_size_t integer);
sieve_size_t sieve_binary_emit_string(struct sieve_binary *binary, const string_t *str);

/* 
 * Code retrieval 
 */

inline const char *sieve_binary_get_code(struct sieve_binary *binary, sieve_size_t *code_size);

#endif
