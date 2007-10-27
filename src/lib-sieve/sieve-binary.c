#include "lib.h"
#include "mempool.h"
#include "buffer.h"
#include "array.h"

#include "sieve-binary.h"

struct sieve_coded_stringlist {
  struct sieve_binary *binary;
  sieve_size_t start_address;
  sieve_size_t end_address;
  sieve_size_t current_offset;
  int length;
  int index;
};

struct sieve_binary {
	pool_t pool;
	ARRAY_DEFINE(extensions, const struct sieve_extension *); 
	buffer_t *data;
	
	const char *code;
	size_t code_size;
};

struct sieve_binary *sieve_binary_create_new(void) 
{
	pool_t pool;
	struct sieve_binary *binary;
	
	pool = pool_alloconly_create("sieve_binary", 4096);	
	binary = p_new(pool, struct sieve_binary, 1);
	binary->pool = pool;
	
	binary->data = buffer_create_dynamic(pool, 256);
	
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

inline sieve_size_t sieve_binary_get_code_size(struct sieve_binary *binary)
{
	return buffer_get_used_size(binary->data);
}

void sieve_binary_commit(struct sieve_binary *binary)
{
	binary->code = buffer_get_data(binary->data, &(binary->code_size));			
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

/* String list */

struct sieve_coded_stringlist *sieve_binary_read_stringlist
  (struct sieve_binary *binary, sieve_size_t *address, bool single)
{
	struct sieve_coded_stringlist *strlist;

  sieve_size_t pc = *address;
  sieve_size_t end; 
  sieve_size_t length = 0; 
 
 	if ( single ) {
 		sieve_size_t strlen;
 		
 		if ( !sieve_binary_read_integer(binary, address, &strlen) ) 
    	return FALSE;
    	
    end = *address + strlen;
    length = 1;
    *address = pc;
	} else {
		int end_offset;
		
  	if ( !sieve_binary_read_offset(binary, address, &end_offset) )
  		return NULL;
  
  	end = pc + end_offset;
  
  	if ( !sieve_binary_read_integer(binary, address, &length) ) 
    	return NULL;
  }
  
  if ( end > binary->code_size ) 
  		return NULL;
    
	strlist = p_new(pool_datastack_create(), struct sieve_coded_stringlist, 1);
	strlist->binary = binary;
	strlist->start_address = *address;
	strlist->current_offset = *address;
	strlist->end_address = end;
	strlist->length = length;
	strlist->index = 0;
  
  /* Skip over the string list for now */
  *address = end;
  
  return strlist;
}

bool sieve_coded_stringlist_next_item(struct sieve_coded_stringlist *strlist, string_t **str) 
{
	sieve_size_t address;
  *str = NULL;
  
  if ( strlist->index >= strlist->length ) 
    return TRUE;
  else {
  	address = strlist->current_offset;
  	
  	if ( sieve_binary_read_string(strlist->binary, &address, str) ) {
    	strlist->index++;
    	strlist->current_offset = address;
    	return TRUE;
    }
  }  
  
  return FALSE;
}

void sieve_coded_stringlist_reset(struct sieve_coded_stringlist *strlist) 
{  
  strlist->current_offset = strlist->start_address;
  strlist->index = 0;
}

inline int sieve_coded_stringlist_get_length(struct sieve_coded_stringlist *strlist)
{
	return strlist->length;
}

inline sieve_size_t sieve_coded_stringlist_get_end_address(struct sieve_coded_stringlist *strlist)
{
	return strlist->end_address;
}

inline sieve_size_t sieve_coded_stringlist_get_current_offset(struct sieve_coded_stringlist *strlist)
{
	return strlist->current_offset;
}




