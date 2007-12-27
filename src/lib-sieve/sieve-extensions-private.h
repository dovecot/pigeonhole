#ifndef __SIEVE_EXTENSIONS_PRIVATE_H
#define __SIEVE_EXTENSIONS_PRIVATE_H

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-binary.h"

/* 
 * Per-extension object declaration
 */

struct sieve_extension_obj_registry {
	const void *objects;
	unsigned int count;
};

#define SIEVE_EXT_DEFINE_NO_OBJECTS \
	{ NULL, 0 }
#define SIEVE_EXT_DEFINE_OBJECT(OBJ) \
	{ &OBJ, 1 }
#define SIEVE_EXT_DEFINE_OBJECTS(OBJS) \
	{ OBJS, N_ELEMENTS(OBJS) }

#define SIEVE_EXT_GET_OBJECTS_COUNT(ext, field) \
	ext->field->count;

static inline const void *_sieve_extension_read_object
(const struct sieve_extension_obj_registry *reg, struct sieve_binary *sbin, 
	sieve_size_t *address)
{ 		
	unsigned int code; 	
		
	if ( reg->count == 1 ) 
		return reg->objects; 
	
	if ( sieve_binary_read_byte(sbin, address, &code) && code < reg->count ) {
		const void * const *objects = (const void * const *) reg->objects;
		return objects[code]; 
	}
	return NULL;
}

#define sieve_extension_read_object\
(ext, type, field, sbin, address, result) \
{ \
	result = NULL; \
	\
	if ( ext != NULL && ext->field.count != 0 ) \
		result = (type *) _sieve_extension_read_object \
			(&ext->field, sbin, address); \
} 

static inline sieve_size_t _sieve_extension_emit_object
(struct sieve_binary *sbin, int ext_id, unsigned char offset)
{
	unsigned char code = offset + 
		sieve_binary_extension_get_index(sbin, ext_id);

	return sieve_binary_emit_byte(sbin, code);
}
#define sieve_extension_emit_object\
(obj, ext_field, sbin, ext_id, offset, address_r) \
{ \
	address_r = _sieve_extension_emit_object(sbin, ext_id, offset); \
	\
	if ( obj->extension->ext_field.count > 1 ) \
		(void) sieve_binary_emit_byte(sbin, obj->code); \
}

/*
 * Extension object
 */

struct sieve_extension {
	const char *name;
	
	bool (*load)(int ext_id);

	bool (*validator_load)(struct sieve_validator *validator);	
	bool (*generator_load)(struct sieve_generator *generator);
	bool (*binary_load)(struct sieve_binary *binary);
	bool (*interpreter_load)(struct sieve_interpreter *interpreter);

	struct sieve_extension_obj_registry opcodes;
	struct sieve_extension_obj_registry operands;
};

/*  
 * Opcodes and operands
 */
 
#define SIEVE_EXT_DEFINE_NO_OPERATIONS SIEVE_EXT_DEFINE_NO_OBJECTS
#define SIEVE_EXT_DEFINE_OPERATION(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_OPERATIONS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

#define SIEVE_EXT_DEFINE_NO_OPERANDS SIEVE_EXT_DEFINE_NO_OBJECTS

/* 
 * Pre-loaded extensions
 */

extern const struct sieve_extension *sieve_preloaded_extensions[];
extern const unsigned int sieve_preloaded_extensions_count;

#endif /* __SIEVE_EXTENSIONS_PRIVATE_H */
