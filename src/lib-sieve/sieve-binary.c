#include "lib.h"
#include "mempool.h"
#include "buffer.h"
#include "array.h"

#include "sieve-binary.h"

struct sieve_binary {
	pool_t pool;
	ARRAY_DEFINE(extensions, const struct sieve_extension *); 
	buffer_t *code;
};

struct sieve_binary *sieve_binary_create_new(void) 
{
	pool_t pool;
	struct sieve_binary *binary;
	
	pool = pool_alloconly_create("sieve_binary", 4096);	
	binary = p_new(pool, struct sieve_binary, 1);
	binary->pool = pool;
	
	binary->code = buffer_create_dynamic(pool, 256);
	
	p_array_init(&binary->extensions, pool, 4);
	
	return binary;
}

void sieve_binary_ref(struct sieve_binary *binary) 
{
	pool_ref(binary->pool);
}

void sieve_binary_unref(struct sieve_binary **binary) 
{
	if ( binary != NULL && *binary != NULL ) {
		pool_unref(&((*binary)->pool));
		*binary = NULL;
	}
}

/* Extension handling */

unsigned int sieve_binary_link_extension(struct sieve_binary *binary, const struct sieve_extension *extension) 
{
	array_append(&(binary->extensions), &extension, 1);
	
	return array_count(&(binary->extensions)) - 1;
}

const struct sieve_extension *sieve_binary_get_extension(struct sieve_binary *binary, unsigned int index) 
{
	const struct sieve_extension * const *ext;
	
	if ( array_count(&(binary->extensions)) > index ) {
		ext = array_idx(&(binary->extensions), index);
		return *ext;
	}
	
	return NULL;
}

/* Low-level emission functions */

inline sieve_size_t sieve_binary_emit_data(struct sieve_binary *binary, void *data, sieve_size_t size) 
{
	sieve_size_t address = buffer_get_used_size(binary->code);
	  
	buffer_append(binary->code, data, size);
	
	return address;
}

inline sieve_size_t sieve_binary_emit_byte(struct sieve_binary *binary, unsigned char byte) 
{
	return sieve_binary_emit_data(binary, &byte, 1);
}

inline void sieve_binary_update_data
	(struct sieve_binary *binary, sieve_size_t address, void *data, sieve_size_t size) 
{
	buffer_write(binary->code, address, data, size);
}

inline sieve_size_t sieve_binary_get_code_size(struct sieve_binary *binary)
{
	return buffer_get_used_size(binary->code);
}

inline const char *sieve_binary_get_code(struct sieve_binary *binary, sieve_size_t *code_size)
{
	return buffer_get_data(binary->code, code_size);		
}

/* Offset emission functions */

sieve_size_t sieve_binary_emit_offset(struct sieve_binary *binary, int offset) 
{
  int i;
	sieve_size_t address = sieve_binary_get_code_size(binary);

  for ( i = 3; i >= 0; i-- ) {
    char c = (char) (offset >> (i * 8));
	  (void) sieve_binary_emit_data(binary, &c, 1);
	}
	
	return address;
}

void sieve_binary_resolve_offset
	(struct sieve_binary *binary, sieve_size_t address) 
{
  int i;
	int offset = sieve_binary_get_code_size(binary) - address; 
	
	for ( i = 3; i >= 0; i-- ) {
    char c = (char) (offset >> (i * 8));
	  (void) sieve_binary_update_data(binary, address + 3 - i, &c, 1);
	}
}

/* Literal emission */

sieve_size_t sieve_binary_emit_integer(struct sieve_binary *binary, sieve_size_t integer)
{
  int i;
  char buffer[sizeof(sieve_size_t) + 1];
  int bufpos = sizeof(buffer) - 1;
  
  buffer[bufpos] = integer & 0x7F;
  bufpos--;
  integer >>= 7;
  while ( integer > 0 ) {
  	buffer[bufpos] = integer & 0x7F;
    bufpos--;
    integer >>= 7;  
  }
  
  bufpos++;
  if ( (sizeof(buffer) - bufpos) > 1 ) { 
    for ( i = bufpos; i < ((int) sizeof(buffer) - 1); i++) {
      buffer[i] |= 0x80;
    }
  } 
  
  return sieve_binary_emit_data(binary, buffer + bufpos, sizeof(buffer) - bufpos);
}

sieve_size_t sieve_binary_emit_string(struct sieve_binary *binary, const string_t *str)
{
	sieve_size_t address = sieve_binary_emit_integer(binary, str_len(str));
  (void) sieve_binary_emit_data(binary, (void *) str_data(str), str_len(str));

  return address;
}
 





