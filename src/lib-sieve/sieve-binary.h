#ifndef __SIEVE_BINARY_H
#define __SIEVE_BINARY_H

#include "lib.h"
#include "str.h"

#include "sieve-common.h"

struct sieve_binary;

struct sieve_binary *sieve_binary_create_new(struct sieve_script *script);
void sieve_binary_ref(struct sieve_binary *sbin);
void sieve_binary_unref(struct sieve_binary **sbin);

inline pool_t sieve_binary_pool(struct sieve_binary *sbin);
inline struct sieve_script *sieve_binary_script(struct sieve_binary *sbin);

void sieve_binary_activate(struct sieve_binary *sbin);

bool sieve_binary_save
	(struct sieve_binary *sbin, const char *path);
	
struct sieve_binary *sieve_binary_open
	(const char *path, struct sieve_script *script);
bool sieve_binary_up_to_date(struct sieve_binary *sbin);
bool sieve_binary_load(struct sieve_binary *sbin);
	
/* 
 * Block management 
 */
 
enum sieve_binary_system_block {
	SBIN_SYSBLOCK_EXTENSIONS,
	SBIN_SYSBLOCK_MAIN_PROGRAM,
	SBIN_SYSBLOCK_LAST
};

bool sieve_binary_block_set_active
	(struct sieve_binary *sbin, unsigned int id, unsigned *old_id_r);
unsigned int sieve_binary_block_create(struct sieve_binary *sbin);
inline void sieve_binary_block_clear
	(struct sieve_binary *sbin, unsigned int id);
	
/* 
 * Extension support 
 */
 
struct sieve_binary_extension {
	const struct sieve_extension *extension;

	bool (*binary_save)(struct sieve_binary *sbin);
	void (*binary_free)(struct sieve_binary *sbin);
	
	bool (*binary_is_up_to_date)(struct sieve_binary *sbin);
};
 
inline void sieve_binary_extension_set_context
	(struct sieve_binary *sbin, int ext_id, void *context);
inline const void *sieve_binary_extension_get_context
	(struct sieve_binary *sbin, int ext_id);
	
inline void sieve_binary_extension_set
	(struct sieve_binary *sbin, int ext_id, 
		const struct sieve_binary_extension *bext);

unsigned int sieve_binary_extension_create_block
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

inline sieve_size_t sieve_binary_emit_data
	(struct sieve_binary *binary, void *data, sieve_size_t size);
inline sieve_size_t sieve_binary_emit_byte
	(struct sieve_binary *binary, unsigned char byte);
inline void sieve_binary_update_data
	(struct sieve_binary *binary, sieve_size_t address, void *data, 
		sieve_size_t size);
inline sieve_size_t sieve_binary_get_code_size(struct sieve_binary *binary);

/* Offset emission functions */

sieve_size_t sieve_binary_emit_offset
	(struct sieve_binary *binary, int offset);
void sieve_binary_resolve_offset
	(struct sieve_binary *binary, sieve_size_t address);

/* Literal emission functions */

sieve_size_t sieve_binary_emit_integer
	(struct sieve_binary *binary, sieve_size_t integer);
sieve_size_t sieve_binary_emit_string
	(struct sieve_binary *binary, const string_t *str);
sieve_size_t sieve_binary_emit_cstring
	(struct sieve_binary *binary, const char *str);

/* Operand emission */

sieve_size_t sieve_binary_emit_operand_id
	(struct sieve_binary *sbin, int operand);
	
/* Opcode emission */

sieve_size_t sieve_binary_emit_opcode_id
	(struct sieve_binary *sbin, int opcode);
sieve_size_t sieve_binary_emit_ext_opcode_id
	(struct sieve_binary *sbin, const struct sieve_extension *extension);
	
/* 
 * Code retrieval 
 */

/* Literals */
bool sieve_binary_read_byte
	(struct sieve_binary *binary, sieve_size_t *address, unsigned int *byte_val);
bool sieve_binary_read_code
	(struct sieve_binary *binary, sieve_size_t *address, int *code);
bool sieve_binary_read_offset
	(struct sieve_binary *binary, sieve_size_t *address, int *offset);
bool sieve_binary_read_integer
  (struct sieve_binary *binary, sieve_size_t *address, sieve_size_t *integer); 
bool sieve_binary_read_string
  (struct sieve_binary *binary, sieve_size_t *address, string_t **str);

/* 
 * Default registry context (used at various occasions)
 */
 
const void *sieve_binary_registry_get_object
	(struct sieve_binary *sbin, int ext_id, int id);
void sieve_binary_registry_set_object
	(struct sieve_binary *sbin, int ext_id, int id, const void *object);
void sieve_binary_registry_init(struct sieve_binary *sbin, int ext_id);

/* These macros are not a very nice solution to the code duplication. I really
 * need to think about this more */

#define sieve_binary_emit_extension(sbin, obj, ext_id, base, operand, mult)   \
	unsigned char code = base + sieve_binary_extension_get_index(sbin, ext_id); \
	                                                                            \
	(void) sieve_operand_emit_code(sbin, operand);                              \
	(void) sieve_binary_emit_byte(sbin, code);                                  \
	if ( mult )                                                                 \
		(void) sieve_binary_emit_byte(sbin, obj->ext_code);                       

#endif
