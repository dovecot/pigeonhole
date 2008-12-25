/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_EXTENSIONS_H
#define __SIEVE_EXTENSIONS_H

#include "lib.h"
#include "sieve-common.h"

/* 
 * Per-extension object registry 
 */

struct sieve_extension_objects {
	const void *objects;
	unsigned int count;
};

/* 
 * Extension object 
 */

struct sieve_extension {
	const char *name;
	const int *id;
	
	bool (*load)(int ext_id);
	void (*unload)(void);

	bool (*validator_load)
		(struct sieve_validator *validator);	
	bool (*generator_load)
		(const struct sieve_codegen_env *cgenv);
	bool (*interpreter_load)
		(const struct sieve_runtime_env *renv, sieve_size_t *address);
	bool (*binary_load)
		(struct sieve_binary *binary);
	
	bool (*binary_dump)
		(struct sieve_dumptime_env *denv);
	bool (*code_dump)
		(const struct sieve_dumptime_env *denv, sieve_size_t *address);

	struct sieve_extension_objects operations;
	struct sieve_extension_objects operands;
};

#define SIEVE_EXT_DEFINE_NO_OBJECTS \
	{ NULL, 0 }
#define SIEVE_EXT_DEFINE_OBJECT(OBJ) \
	{ &OBJ, 1 }
#define SIEVE_EXT_DEFINE_OBJECTS(OBJS) \
	{ OBJS, N_ELEMENTS(OBJS) }

#define SIEVE_EXT_GET_OBJECTS_COUNT(ext, field) \
	ext->field->count;

/* 
 * Defining opcodes and operands 
 */

#define SIEVE_EXT_DEFINE_NO_OPERATIONS SIEVE_EXT_DEFINE_NO_OBJECTS
#define SIEVE_EXT_DEFINE_OPERATION(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_OPERATIONS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

#define SIEVE_EXT_DEFINE_NO_OPERANDS SIEVE_EXT_DEFINE_NO_OBJECTS
#define SIEVE_EXT_DEFINE_OPERAND(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_OPERANDS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

/* 
 * Pre-loaded extensions 
 */

extern const struct sieve_extension *sieve_preloaded_extensions[];
extern const unsigned int sieve_preloaded_extensions_count;

/*  
 * Extensions init/deinit 
 */

bool sieve_extensions_init(void);
void sieve_extensions_deinit(void);

/* 
 * Extension registry 
 */

int sieve_extension_register(const struct sieve_extension *extension);
int sieve_extensions_get_count(void);
const struct sieve_extension *sieve_extension_get_by_id(unsigned int ext_id);
const struct sieve_extension *sieve_extension_get_by_name(const char *name);

const char *sieve_extensions_get_string(void);
void sieve_extensions_set_string(const char *ext_string);

/*
 * Capability registries
 */

struct sieve_extension_capabilities {
	const char *name;

	const char *(*get_string)(void);	
};

void sieve_extension_capabilities_register
	(const struct sieve_extension_capabilities *cap);
const char *sieve_extension_capabilities_get_string
	(const char *cap_name);

#endif /* __SIEVE_EXTENSIONS_H */
