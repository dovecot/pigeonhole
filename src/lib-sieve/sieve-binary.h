#ifndef __SIEVE_BINARY_H
#define __SIEVE_BINARY_H

#include "lib.h"
#include "str.h"

#include "sieve-common.h"

struct sieve_binary;

struct sieve_binary *sieve_binary_create_new(void);
void sieve_binary_ref(struct sieve_binary *sbin);
void sieve_binary_unref(struct sieve_binary **sbin);

void sieve_binary_load(struct sieve_binary *sbin);

/* 
 * Extension support 
 */
 
inline void sieve_binary_extension_set_context
	(struct sieve_binary *sbin, int ext_id, void *context);
inline const void *sieve_binary_extension_get_context
	(struct sieve_binary *sbin, int ext_id);

int sieve_binary_extension_link
	(struct sieve_binary *sbin, int ext_id);
const struct sieve_extension *sieve_binary_extension_get_by_index
	(struct sieve_binary *sbin, int index, int *ext_id);
int sieve_binary_extension_get_index
	(struct sieve_binary *sbin, int ext_id);
int sieve_binary_extensions_count(struct sieve_binary *sbin);
	
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

/* Operand emission */

sieve_size_t sieve_binary_emit_operand_id(struct sieve_binary *sbin, int operand);
	
/* Opcode emission */

sieve_size_t sieve_binary_emit_opcode_id(struct sieve_binary *sbin, int opcode);
sieve_size_t sieve_binary_emit_ext_opcode_id
	(struct sieve_binary *sbin, const struct sieve_extension *extension);
	
/* 
 * Code retrieval 
 */

/* Literals */
bool sieve_binary_read_byte
	(struct sieve_binary *binary, sieve_size_t *address, unsigned int *byte_val);
bool sieve_binary_read_offset
	(struct sieve_binary *binary, sieve_size_t *address, int *offset);
bool sieve_binary_read_integer
  (struct sieve_binary *binary, sieve_size_t *address, sieve_size_t *integer); 
bool sieve_binary_read_string
  (struct sieve_binary *binary, sieve_size_t *address, string_t **str);

#endif
