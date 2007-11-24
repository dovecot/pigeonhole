#include "lib.h"

#include "mempool.h"
#include "buffer.h"
#include "hash.h"
#include "array.h"

#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-binary.h"

struct sieve_binary_extension {
	const struct sieve_extension *extension;
	int ext_id;
	int index;
};

struct sieve_binary {
	pool_t pool;
	
	ARRAY_DEFINE(ext_contexts, void *); 
	
	ARRAY_DEFINE(extensions, struct sieve_binary_extension *); 
	ARRAY_DEFINE(extension_index, struct sieve_binary_extension *); 
	
	buffer_t *data;
	
	const char *code;
	size_t code_size;
};

struct sieve_binary *sieve_binary_create_new(void) 
{
	pool_t pool;
	struct sieve_binary *sbin;
	
	pool = pool_alloconly_create("sieve_binary", 4096);	
	sbin = p_new(pool, struct sieve_binary, 1);
	sbin->pool = pool;
	
	sbin->data = buffer_create_dynamic(pool, 256);
	
	p_array_init(&sbin->ext_contexts, pool, 5);
	
	p_array_init(&sbin->extensions, pool, 5);
	p_array_init(&sbin->extension_index, pool, sieve_extensions_get_count());
		
	return sbin;
}

void sieve_binary_ref(struct sieve_binary *sbin) 
{
	pool_ref(sbin->pool);
}

void sieve_binary_unref(struct sieve_binary **sbin) 
{
	if ( sbin != NULL && *sbin != NULL ) {
		pool_unref(&((*sbin)->pool));
	}
	*sbin = NULL;
}

inline sieve_size_t sieve_binary_get_code_size(struct sieve_binary *sbin)
{
	return buffer_get_used_size(sbin->data);
}

inline pool_t sieve_binary_pool(struct sieve_binary *sbin)
{
	return sbin->pool;
}

void sieve_binary_load(struct sieve_binary *sbin)
{
	unsigned int i;
	
	/* Currently only memory binary support */
	sbin->code = buffer_get_data(sbin->data, &(sbin->code_size));			
	
	/* Pre-load core language features implemented as 'extensions' */
	for ( i = 0; i < sieve_preloaded_extensions_count; i++ ) {
		const struct sieve_extension *ext = sieve_preloaded_extensions[i];
		
		if ( ext->binary_load != NULL )
			(void)ext->binary_load(sbin);		
	}
	
	/* Load other extensions into binary */
	for ( i = 0; i < array_count(&sbin->extensions); i++ ) {
		struct sieve_binary_extension * const *aext = 
			array_idx(&sbin->extensions, i);
		const struct sieve_extension *ext = (*aext)->extension;
		
		if ( ext->binary_load != NULL )
			ext->binary_load(sbin);
	}
}

/* Extension contexts */

inline void sieve_binary_extension_set_context
	(struct sieve_binary *sbin, int ext_id, void *context)
{
	array_idx_set(&sbin->ext_contexts, (unsigned int) ext_id, &context);	
}

inline const void *sieve_binary_extension_get_context
	(struct sieve_binary *sbin, int ext_id) 
{
	void * const *ctx;

	if  ( ext_id < 0 || ext_id >= (int) array_count(&sbin->ext_contexts) )
		return NULL;
	
	ctx = array_idx(&sbin->ext_contexts, (unsigned int) ext_id);		

	return *ctx;
}

/* Extension handling */

int sieve_binary_extension_link
	(struct sieve_binary *sbin, int ext_id) 
{
	int index = array_count(&sbin->extensions);
	const struct sieve_extension *ext = sieve_extension_get_by_id(ext_id); 
	struct sieve_binary_extension *bext;

	if ( ext != NULL ) {
		bext = p_new(sbin->pool, struct sieve_binary_extension, 1);
		bext->index = index;
		bext->ext_id = ext_id;
		bext->extension = ext;
	
		array_idx_set(&sbin->extensions, (unsigned int) index, &bext);
		array_idx_set(&sbin->extension_index, (unsigned int) ext_id, &bext);
	
		return index;
	}
	
	return -1;
}

const struct sieve_extension *sieve_binary_extension_get_by_index
	(struct sieve_binary *sbin, int index, int *ext_id) 
{
	struct sieve_binary_extension * const *ext;
	
	if ( index < (int) array_count(&sbin->extensions) ) {
		ext = array_idx(&sbin->extensions, (unsigned int) index);
		
		if ( ext_id != NULL ) *ext_id = (*ext)->ext_id;
		
		return (*ext)->extension;
	}
	
	if ( ext_id != NULL ) *ext_id = -1;
	
	return NULL;
}

int sieve_binary_extension_get_index
	(struct sieve_binary *sbin, int ext_id) 
{
	struct sieve_binary_extension * const *ext;
	
	if ( ext_id < (int) array_count(&sbin->extension_index) ) {
		ext = array_idx(&sbin->extension_index, (unsigned int) ext_id);
		
		return (*ext)->index;
	}
	
	return -1;
}

int sieve_binary_extensions_count(struct sieve_binary *sbin) 
{
	return (int) array_count(&sbin->extensions);
}

/*
 * Emission functions
 */

/* Low-level emission functions */

inline sieve_size_t sieve_binary_emit_data(struct sieve_binary *binary, void *data, sieve_size_t size) 
{
	sieve_size_t address = buffer_get_used_size(binary->data);
	  
	buffer_append(binary->data, data, size);
	
	return address;
}

inline sieve_size_t sieve_binary_emit_byte(struct sieve_binary *binary, unsigned char byte) 
{
	return sieve_binary_emit_data(binary, &byte, 1);
}

inline void sieve_binary_update_data
	(struct sieve_binary *binary, sieve_size_t address, void *data, sieve_size_t size) 
{
	buffer_write(binary->data, address, data, size);
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

/*
 * Code retrieval
 */
 
#define ADDR_CODE_AT(binary, address) (binary->code[*address])
#define ADDR_DATA_AT(binary, address) ((unsigned char) (binary->code[*address]))
#define ADDR_BYTES_LEFT(binary, address) (binary->code_size - (*address))
#define ADDR_JUMP(address, offset) (*address) += offset

/* Literals */

bool sieve_binary_read_byte
	(struct sieve_binary *binary, sieve_size_t *address, unsigned int *byte_val) 
{	
	if ( ADDR_BYTES_LEFT(binary, address) >= 1 ) {
		if ( byte_val != NULL )
			*byte_val = ADDR_DATA_AT(binary, address);
		ADDR_JUMP(address, 1);
			
		return TRUE;
	}
	
	return FALSE;
}

bool sieve_binary_read_offset
	(struct sieve_binary *binary, sieve_size_t *address, int *offset) 
{
	uint32_t offs = 0;
	
	if ( ADDR_BYTES_LEFT(binary, address) >= 4 ) {
	  int i; 
	  
	  for ( i = 0; i < 4; i++ ) {
	    offs = (offs << 8) + ADDR_DATA_AT(binary, address);
	  	ADDR_JUMP(address, 1);
	  }
	  
	  if ( offset != NULL )
			*offset = (int) offs;
			
		return TRUE;
	}
	
	return FALSE;
}

bool sieve_binary_read_integer
  (struct sieve_binary *binary, sieve_size_t *address, sieve_size_t *integer) 
{
  int bits = sizeof(sieve_size_t) * 8;
  *integer = 0;
  
  while ( (ADDR_DATA_AT(binary, address) & 0x80) > 0 ) {
    if ( ADDR_BYTES_LEFT(binary, address) > 0 && bits > 0) {
      *integer |= ADDR_DATA_AT(binary, address) & 0x7F;
      ADDR_JUMP(address, 1);
    
      *integer <<= 7;
      bits -= 7;
    } else {
      /* This is an error */
      return FALSE;
    }
  }
  
  *integer |= ADDR_DATA_AT(binary, address) & 0x7F;
  ADDR_JUMP(address, 1);
  
  return TRUE;
}

/* FIXME: add this to lib/str. */
static string_t *t_str_const(const void *cdata, size_t size)
{
	string_t *result = t_str_new(size);
	
	str_append_n(result, cdata, size);
	
	return result;
	//return buffer_create_const_data(pool_datastack_create(), cdata, size);
}

bool sieve_binary_read_string
  (struct sieve_binary *binary, sieve_size_t *address, string_t **str) 
{
	sieve_size_t strlen = 0;
  
	if ( !sieve_binary_read_integer(binary, address, &strlen) ) 
		return FALSE;
    	  
	if ( strlen > ADDR_BYTES_LEFT(binary, address) ) 
		return FALSE;
   
	*str = t_str_const(&ADDR_CODE_AT(binary, address), strlen);
	ADDR_JUMP(address, strlen);
  
	return TRUE;
}

