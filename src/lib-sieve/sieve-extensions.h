#ifndef __SIEVE_EXTENSIONS_H
#define __SIEVE_EXTENSIONS_H

#include "lib.h"
#include "sieve-common.h"

/* Per-extension object registry */

struct sieve_extension_obj_registry {
	const void *objects;
	unsigned int count;
};

/* Extension object */

struct sieve_extension {
	const char *name;
	const int *id;
	
	bool (*load)(int ext_id);

	bool (*validator_load)(struct sieve_validator *validator);	
	bool (*generator_load)(struct sieve_generator *generator);
	bool (*interpreter_load)(struct sieve_interpreter *interpreter);
	
	bool (*runtime_load)(const struct sieve_runtime_env *renv);
	
	bool (*binary_load)(struct sieve_binary *binary);
	bool (*binary_dump)(struct sieve_dumptime_env *denv);

	struct sieve_extension_obj_registry operations;
	struct sieve_extension_obj_registry operands;
};

#define SIEVE_EXT_DEFINE_NO_OBJECTS \
	{ NULL, 0 }
#define SIEVE_EXT_DEFINE_OBJECT(OBJ) \
	{ &OBJ, 1 }
#define SIEVE_EXT_DEFINE_OBJECTS(OBJS) \
	{ OBJS, N_ELEMENTS(OBJS) }

#define SIEVE_EXT_GET_OBJECTS_COUNT(ext, field) \
	ext->field->count;

/* Opcodes and operands */

#define SIEVE_EXT_DEFINE_NO_OPERATIONS SIEVE_EXT_DEFINE_NO_OBJECTS
#define SIEVE_EXT_DEFINE_OPERATION(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_OPERATIONS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

#define SIEVE_EXT_DEFINE_NO_OPERANDS SIEVE_EXT_DEFINE_NO_OBJECTS
#define SIEVE_EXT_DEFINE_OPERAND(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_OPERANDS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

/* Pre-loaded extensions */

extern const struct sieve_extension *sieve_preloaded_extensions[];
extern const unsigned int sieve_preloaded_extensions_count;


const struct sieve_extension *sieve_extension_acquire(const char *extension);

/* Extensions state */

bool sieve_extensions_init(const char *sieve_plugins ATTR_UNUSED);
const int *sieve_extensions_get_preloaded_ext_ids(void);
void sieve_extensions_deinit(void);

/* Extension registry */

int sieve_extension_register(const struct sieve_extension *extension);
int sieve_extensions_get_count(void);
const struct sieve_extension *sieve_extension_get_by_id(unsigned int ext_id);
int sieve_extension_get_by_name(const char *name, const struct sieve_extension **ext);
int sieve_extension_get_id(const struct sieve_extension *extension);

const char *sieve_extensions_get_string(void);

#endif /* __SIEVE_EXTENSIONS_H */
