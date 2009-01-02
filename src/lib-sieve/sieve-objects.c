/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"
#include "sieve-interpreter.h"

#include "sieve-objects.h"

/*
 * Object coding
 */

void sieve_opr_object_emit
(struct sieve_binary *sbin, const struct sieve_object *obj)
{
	struct sieve_extension_objects *objs = 
		(struct sieve_extension_objects *) obj->operand->interface;
		 
	(void) sieve_operand_emit_code(sbin, obj->operand);
	
	if ( objs->count > 1 ) {	
		(void) sieve_binary_emit_byte(sbin, obj->code);
	} 
}

const struct sieve_object *sieve_opr_object_read_data
(struct sieve_binary *sbin, const struct sieve_operand *operand,
	const struct sieve_operand_class *opclass, sieve_size_t *address)
{
	const struct sieve_extension_objects *objs;
	unsigned int obj_code; 

	if ( operand == NULL || operand->class != opclass )
		return NULL;
	
	objs = (struct sieve_extension_objects *) operand->interface;
	if ( objs == NULL ) 
		return NULL;
			
	if ( objs->count > 1 ) {
		if ( !sieve_binary_read_byte(sbin, address, &obj_code) ) 
			return NULL;

		if ( obj_code < objs->count ) {
			const struct sieve_object *const *objects = 
				(const struct sieve_object* const *) objs->objects;
			return objects[obj_code]; 
		}
	}
	
	return (const struct sieve_object *) objs->objects; 
}

const struct sieve_object *sieve_opr_object_read
(const struct sieve_runtime_env *renv, 
	const struct sieve_operand_class *opclass, sieve_size_t *address)
{
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);
	
	return sieve_opr_object_read_data(renv->sbin, operand, opclass, address);
}

bool sieve_opr_object_dump
(const struct sieve_dumptime_env *denv, 
	const struct sieve_operand_class *opclass, sieve_size_t *address,
	const struct sieve_object **object_r)
{
	const struct sieve_operand *operand;
	const struct sieve_object *obj;
	const char *class;
	
	sieve_code_mark(denv);
	
	operand = sieve_operand_read(denv->sbin, address); 
	obj = sieve_opr_object_read_data(denv->sbin, operand, opclass, address);
	
	if ( obj == NULL )
		return FALSE;
		
	if ( operand->class == NULL )
		class = "OBJECT";
	else
		class = operand->class->name;
			
	sieve_code_dumpf(denv, "%s: %s", class, obj->identifier);
	
	if ( object_r != NULL )
		*object_r = obj;
	
	return TRUE;
}

