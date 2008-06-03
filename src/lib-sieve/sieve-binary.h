#ifndef __SIEVE_BINARY_H
#define __SIEVE_BINARY_H

#include "lib.h"
#include "str.h"

#include "sieve-common.h"

struct sieve_binary;

struct sieve_binary *sieve_binary_create_new(struct sieve_script *script);
void sieve_binary_ref(struct sieve_binary *sbin);
void sieve_binary_unref(struct sieve_binary **sbin);

pool_t sieve_binary_pool(struct sieve_binary *sbin);
struct sieve_script *sieve_binary_script(struct sieve_binary *sbin);
const char *sieve_binary_path(struct sieve_binary *sbin);
bool sieve_binary_script_older
	(struct sieve_binary *sbin, struct sieve_script *script);

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
void sieve_binary_block_clear
	(struct sieve_binary *sbin, unsigned int id);
	
/* 
 * Extension support 
 */
 
struct sieve_binary_extension {
	const struct sieve_extension *extension;

	bool (*binary_save)(struct sieve_binary *sbin);
	bool (*binary_open)(struct sieve_binary *sbin);
	
	void (*binary_free)(struct sieve_binary *sbin);
	
	bool (*binary_up_to_date)(struct sieve_binary *sbin);
};
 
void sieve_binary_extension_set_context
	(struct sieve_binary *sbin, int ext_id, void *context);
const void *sieve_binary_extension_get_context
	(struct sieve_binary *sbin, int ext_id);
	
void sieve_binary_extension_set
	(struct sieve_binary *sbin, int ext_id, 
		const struct sieve_binary_extension *bext);

unsigned int sieve_binary_extension_create_block
	(struct sieve_binary *sbin, int ext_id);
unsigned int sieve_binary_extension_get_block
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

sieve_size_t sieve_binary_emit_data
	(struct sieve_binary *binary, const void *data, sieve_size_t size);
sieve_size_t sieve_binary_emit_byte
	(struct sieve_binary *binary, unsigned char byte);
void sieve_binary_update_data
	(struct sieve_binary *binary, sieve_size_t address, const void *data, 
		sieve_size_t size);
sieve_size_t sieve_binary_get_code_size(struct sieve_binary *binary);

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

#endif
