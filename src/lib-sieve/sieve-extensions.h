#ifndef SIEVE_EXTENSIONS_H
#define SIEVE_EXTENSIONS_H

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
 * Extension definition
 */

struct sieve_extension_def {
	const char *name;

	/* Version */
	unsigned int version;

	/* Registration */
	int (*load)(const struct sieve_extension *ext, void **context_r);
	void (*unload)(const struct sieve_extension *ext);

	/* Compilation */
	bool (*validator_load)(const struct sieve_extension *ext,
			       struct sieve_validator *validator);
	bool (*generator_load)(const struct sieve_extension *ext,
			       const struct sieve_codegen_env *cgenv);
	bool (*interpreter_load)(const struct sieve_extension *ext,
				 const struct sieve_runtime_env *renv,
				 sieve_size_t *address);
	bool (*binary_load)(const struct sieve_extension *ext,
			    struct sieve_binary *binary);

	/* Code dump */
	bool (*binary_dump)(const struct sieve_extension *ext,
			    struct sieve_dumptime_env *denv);
	bool (*code_dump)(const struct sieve_extension *ext,
			  const struct sieve_dumptime_env *denv,
			  sieve_size_t *address);

	/* Objects */
	struct sieve_extension_objects operations;
	struct sieve_extension_objects operands;
};

/* Defining opcodes and operands */

#define SIEVE_EXT_DEFINE_NO_OBJECTS \
	{ NULL, 0 }
#define SIEVE_EXT_DEFINE_OBJECT(OBJ) \
	{ &OBJ, 1 }
#define SIEVE_EXT_DEFINE_OBJECTS(OBJS) \
	{ OBJS, N_ELEMENTS(OBJS) }

#define SIEVE_EXT_GET_OBJECTS_COUNT(ext, field) \
	ext->field->count;

#define SIEVE_EXT_DEFINE_NO_OPERATIONS \
	.operations = SIEVE_EXT_DEFINE_NO_OBJECTS
#define SIEVE_EXT_DEFINE_OPERATION(OP) \
	.operations = SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_OPERATIONS(OPS) \
	.operations = SIEVE_EXT_DEFINE_OBJECTS(OPS)

#define SIEVE_EXT_DEFINE_NO_OPERANDS \
	.operands = SIEVE_EXT_DEFINE_NO_OBJECTS
#define SIEVE_EXT_DEFINE_OPERAND(OP) \
	.operands = SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_OPERANDS(OPS) \
	.operands = SIEVE_EXT_DEFINE_OBJECTS(OPS)

/*
 * Extension instance
 */

struct sieve_extension {
	const struct sieve_extension_def *def;
	int id;

	struct sieve_instance *svinst;
	void *context;

	bool required:1;
	bool loaded:1;
	bool enabled:1;
	bool dummy:1;
	bool global:1;
	bool implicit:1;
	bool overridden:1;
};

#define sieve_extension_is(ext, definition) \
	((ext)->def == &(definition))
#define sieve_extension_name(ext) \
	((ext)->def->name)
#define sieve_extension_name_is(ext, _name) \
	( strcmp((ext)->def->name, (_name)) == 0 )
#define sieve_extension_version(ext) \
	((ext)->def->version)
#define sieve_extension_version_is(ext, _version) \
	((ext)->def->version == (_version))

/*
 * Extensions init/deinit
 */

int sieve_extensions_init(struct sieve_instance *svinst);
int sieve_extensions_load(struct sieve_instance *svinst);
void sieve_extensions_deinit(struct sieve_instance *svinst);

/*
 * Pre-loaded extensions
 */

const struct sieve_extension *const *
sieve_extensions_get_preloaded(struct sieve_instance *svinst,
			       unsigned int *count_r);

/*
 * Extension registry
 */

int sieve_extension_register(struct sieve_instance *svinst,
			     const struct sieve_extension_def *extdef,
			     bool load, const struct sieve_extension **ext_r);
int sieve_extension_require(struct sieve_instance *svinst,
			    const struct sieve_extension_def *extdef, bool load,
			    const struct sieve_extension **ext_r);
int sieve_extension_reload(const struct sieve_extension *ext);

void sieve_extension_unregister(const struct sieve_extension *ext);

int sieve_extension_replace(struct sieve_instance *svinst,
			    const struct sieve_extension_def *extdef, bool load,
			    const struct sieve_extension **ext_r);
void sieve_extension_override(struct sieve_instance *svinst, const char *name,
			      const struct sieve_extension *ext);

unsigned int sieve_extensions_get_count(struct sieve_instance *svinst);
const struct sieve_extension *const *
sieve_extensions_get_all(struct sieve_instance *svinst, unsigned int *count_r);

const struct sieve_extension *
sieve_extension_get_by_id(struct sieve_instance *svinst, unsigned int ext_id);
const struct sieve_extension *
sieve_extension_get_by_name(struct sieve_instance *svinst, const char *name);

const char *sieve_extensions_get_string(struct sieve_instance *svinst);
int sieve_extensions_set_string(struct sieve_instance *svinst,
				const char *ext_string, bool global,
				bool implicit);

const struct sieve_extension *
sieve_get_match_type_extension(struct sieve_instance *svinst);
const struct sieve_extension *
sieve_get_comparator_extension(struct sieve_instance *svinst);
const struct sieve_extension *
sieve_get_address_part_extension(struct sieve_instance *svinst);

void sieve_enable_debug_extension(struct sieve_instance *svinst);

/*
 * Capability registries
 */

struct sieve_extension_capabilities {
	const char *name;

	const char *(*get_string)(const struct sieve_extension *ext);
};

void sieve_extension_capabilities_register(
	const struct sieve_extension *ext,
	const struct sieve_extension_capabilities *cap);
void sieve_extension_capabilities_unregister(const struct sieve_extension *ext);

const char *
sieve_extension_capabilities_get_string(struct sieve_instance *svinst,
					const char *cap_name);

#endif
