#ifndef __SIEVE_EXTENSIONS_PRIVATE_H
#define __SIEVE_EXTENSIONS_PRIVATE_H

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-binary.h"

/* 
 * Per-extension object declaration
 */

static inline const void *_sieve_extension_get_object
(const struct sieve_extension_obj_registry *reg, unsigned int code)
{ 		
	if ( reg->count == 1 ) 
		return reg->objects; 
	
	if ( code < reg->count ) {
		const void * const *objects = (const void * const *) reg->objects;
		return objects[code]; 
	}
	return NULL;
}

static inline sieve_size_t _sieve_extension_emit_obj
(struct sieve_binary *sbin, 
	const struct sieve_extension_obj_registry *defreg,
	const struct sieve_extension_obj_registry *reg,
	unsigned int obj_code, int ext_id) 
{ 
	if ( ext_id >= 0 ) {
		sieve_size_t address; 
		unsigned char code = defreg->count +
			sieve_binary_extension_get_index(sbin, ext_id);

		address = sieve_binary_emit_byte(sbin, code);
		
		if ( reg->count > 1 ) 
			(void) sieve_binary_emit_byte(sbin, obj_code);
	
		return address;
	} 
	
	return sieve_binary_emit_byte(sbin, obj_code);
}
#define sieve_extension_emit_obj(sbin, defreg, obj, reg, ext_id)\
	_sieve_extension_emit_obj\
		(sbin, defreg, &obj->extension->reg, obj->code, ext_id)

static inline const void *_sieve_extension_read_obj
(struct sieve_binary *sbin, sieve_size_t *address, 
	const struct sieve_extension_obj_registry *defreg, 
	const struct sieve_extension_obj_registry *(*get_reg_func)
		(struct sieve_binary *sbin, unsigned int index)) 
{ 
	unsigned int obj_code; 

	if ( sieve_binary_read_byte(sbin, address, &obj_code) ) { 
		if ( obj_code < defreg->count ) { 
			return _sieve_extension_get_object(defreg, obj_code); 
		} else {
			unsigned int code = 0; 	 
			const struct sieve_extension_obj_registry *reg;
		
			if ( (reg=get_reg_func(sbin, obj_code - defreg->count)) == NULL || 
				reg->count == 0 ) 
				return NULL; 
		
			if ( reg->count > 1) 
				sieve_binary_read_byte(sbin, address, &code); 
	
			return _sieve_extension_get_object(reg, code);
		}
	}
	
	return NULL;
}
#define sieve_extension_read_obj(type, sbin, address, defreg, get_reg_func) \
	((const type *) _sieve_extension_read_obj \
		(sbin, address, defreg, get_reg_func))

static inline const char *sieve_extension_read_obj_string
(struct sieve_binary *sbin, sieve_size_t *address, 
	const struct sieve_extension_obj_registry *defreg, 
	const struct sieve_extension_obj_registry *(*get_reg_func)
		(struct sieve_binary *sbin, unsigned int index)) 
{ 
	unsigned int obj_code; 

	if ( sieve_binary_read_byte(sbin, address, &obj_code) ) { 
		if ( obj_code < defreg->count ) { 
			return t_strdup_printf("[CODE: %d]", obj_code); 
		} else {
			unsigned int code = 0; 	 
			const struct sieve_extension_obj_registry *reg;
		
			if ( (reg=get_reg_func(sbin, obj_code - defreg->count)) == NULL ||
				reg->count == 0 ) 
				return t_strdup_printf("[EXT: %d; NO CODES!]", obj_code);
		
			if ( reg->count > 1) 
				sieve_binary_read_byte(sbin, address, &code); 
	
			return t_strdup_printf("[EXT: %d; CODE: %d]", obj_code, code);
		}
	}
	
	return NULL;
}

#endif /* __SIEVE_EXTENSIONS_PRIVATE_H */
