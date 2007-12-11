#include "lib.h"

#include "mempool.h"
#include "buffer.h"
#include "hash.h"
#include "array.h"
#include "ostream.h"

#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-binary.h"

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define SIEVE_BINARY_MAGIC              0xdeadbeaf
#define SIEVE_BINARY_MAGIC_OTHER_ENDIAN 0xefbeadde 

#define SIEVE_BINARY_VERSION_MAJOR 0
#define SIEVE_BINARY_VERSION_MINOR 0

#define SIEVE_BINARY_ALIGN(offset) \
	(((offset) + 3) & ~3)
#define SIEVE_BINARY_ALIGN_PTR(ptr) \
	((void *) SIEVE_BINARY_ALIGN(((size_t) ptr)))

/* Forward declarations */
static inline sieve_size_t sieve_binary_emit_dynamic_data
	(struct sieve_binary *binary, void *data, size_t size);
static inline sieve_size_t sieve_binary_emit_cstring
	(struct sieve_binary *binary, const char *str);

struct sieve_binary_extension {
	const struct sieve_extension *extension;
	int ext_id;
	int index;
};

struct sieve_binary {
	pool_t pool;
	int refcount;
	
	struct sieve_script *script;
	
	/* When the binary is loaded into memory or when it is being constructed by
	 * the generator, extensions will be associated to the binary. The extensions
	 * array is a sequential lit of all used extensions. The extension_index 
	 * array is a mapping extension_id -> binary_extension. This is used to obtain
	 * the code associated with an extension for this particular binary. 
	 */
	ARRAY_DEFINE(extensions, struct sieve_binary_extension *); 
	ARRAY_DEFINE(extension_index, struct sieve_binary_extension *); 
	
	/* Upon loading a binary, the 'require'd extensions will sometimes need to
	 * associate context data to the binary object in memory. This is stored in 
	 * the following array:
	 */
	ARRAY_DEFINE(ext_contexts, void *); 
	
	/* Attributes of a loaded binary */
	const char *path;
	
	/* Pointer to the binary in memory (could be mmap()ed as well)
	 * This is only set when the binary is read from disk and not live-generated. 
	 */
	const void *memory;
	const void *memory_end;
	
	/* Blocks */
	ARRAY_DEFINE(blocks, buffer_t *); 
	unsigned int active_block;
	
	/* Current block buffer: all emit and read functions act upon this buffer */
	buffer_t *data;
	const char *code;
	size_t code_size;
};

static struct sieve_binary *sieve_binary_create(struct sieve_script *script) 
{
	pool_t pool;
	struct sieve_binary *sbin;
	
	pool = pool_alloconly_create("sieve_binary", 4096);	
	sbin = p_new(pool, struct sieve_binary, 1);
	sbin->pool = pool;
	sbin->refcount = 1;
	sbin->script = script;
	
	p_array_init(&sbin->extensions, pool, 5);
	p_array_init(&sbin->extension_index, pool, sieve_extensions_get_count());
	
	p_array_init(&sbin->ext_contexts, pool, 5);
	
	p_array_init(&sbin->blocks, pool, 3);
		
	return sbin;
}

struct sieve_binary *sieve_binary_create_new(struct sieve_script *script) 
{
	struct sieve_binary *sbin = sieve_binary_create(script); 
	
	/* Extensions block */
	(void) sieve_binary_block_create(sbin);
	
	/* Main program block */
	sieve_binary_block_set_active(sbin, sieve_binary_block_create(sbin));
	
	return sbin;
}

void sieve_binary_ref(struct sieve_binary *sbin) 
{
	sbin->refcount++;
}

void sieve_binary_unref(struct sieve_binary **sbin) 
{
	i_assert((*sbin)->refcount > 0);

	if (--(*sbin)->refcount != 0)
		return;

	pool_unref(&((*sbin)->pool));
	
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

inline struct sieve_script *sieve_binary_script(struct sieve_binary *sbin)
{
	return sbin->script;
}

/* Block management */

static inline buffer_t *sieve_binary_block_get
	(struct sieve_binary *sbin, unsigned int id) 
{
	buffer_t * const *block;

	if  ( id >= array_count(&sbin->blocks) )
		return NULL;
	
	block = array_idx(&sbin->blocks, id);		

	return *block;
}

static inline unsigned int sieve_binary_block_add
	(struct sieve_binary *sbin, buffer_t *blockbuf)
{
	unsigned int id = array_count(&sbin->blocks);
	
	array_append(&sbin->blocks, &blockbuf, 1);	
	return id;
}

static inline unsigned int sieve_binary_block_count
	(struct sieve_binary *sbin)
{
	return array_count(&sbin->blocks);
}

unsigned int sieve_binary_block_set_active
	(struct sieve_binary *sbin, unsigned int id)
{
	unsigned int old_id = sbin->active_block;
	buffer_t *buffer = sieve_binary_block_get(sbin, id);
	
	if ( buffer != NULL ) {
		sbin->data = buffer;
		sbin->code = buffer_get_data(buffer, &sbin->code_size);
		sbin->active_block = id;
	}
	
	return old_id;
}

unsigned int sieve_binary_block_create(struct sieve_binary *sbin)
{
	buffer_t *buffer = buffer_create_dynamic(sbin->pool, 64);

	return sieve_binary_block_add(sbin, buffer);
}

/* Saving and loading the binary to/from a file. */

struct sieve_binary_header {
	uint32_t magic;
	uint16_t version_major;
	uint16_t version_minor;
	uint32_t blocks;
};

struct sieve_binary_block_header {
	uint32_t id; /* Redundant, just for sanity checking */
	uint32_t size;
};

/* FIXME: Is this even necessary for a file? */
static bool _save_full(struct ostream *stream, const void *data, size_t size)
{
	size_t bytes_left = size;
	const void *pdata = data;
	
	while ( bytes_left > 0 ) {
		ssize_t ret;
		
		if ( (ret=o_stream_send(stream, pdata, bytes_left)) <= 0 ) 
			return FALSE;
			
		pdata = PTR_OFFSET(pdata, ret);
		bytes_left -= ret;
	}	
	
	return TRUE;
}

static bool _save_aligned(struct ostream *stream, const void *data, size_t size)
{
	uoff_t aligned_offset = SIEVE_BINARY_ALIGN(stream->offset);
	
	o_stream_cork(stream);
	
	/* Align the data by adding zeroes to the output stream */
	if ( stream->offset < aligned_offset ) {
		if ( !_save_full(stream, (void *) "\0\0\0\0\0\0\0\0",
			aligned_offset - stream->offset) ) 
			return FALSE;
	}
	
	if ( !_save_full(stream, data, size) )
		return FALSE;
	
	o_stream_uncork(stream); 

	return TRUE;
} 

static bool _save_block
(struct sieve_binary *sbin, struct ostream *stream, unsigned int id)
{
	struct sieve_binary_block_header block_header;
	buffer_t *block;
	const void *data;
	size_t size;
		
	block = sieve_binary_block_get(sbin, id);
	if ( block == NULL )
		return FALSE;
		
	data = buffer_get_data(block, &size);
	
	block_header.id = id;
	block_header.size = size;
	
	if ( !_save_aligned(stream, &block_header, sizeof(block_header)) )
		return FALSE;
	
	return _save_aligned(stream, data, size);
}

static bool _sieve_binary_save
	(struct sieve_binary *sbin, struct ostream *stream)
{
	struct sieve_binary_header header;
	unsigned int ext_count, i;
	
	/* Create header */
	
	header.magic = SIEVE_BINARY_MAGIC;
	header.version_major = SIEVE_BINARY_VERSION_MAJOR;
	header.version_minor = SIEVE_BINARY_VERSION_MINOR;
	header.blocks = sieve_binary_block_count(sbin);

	if ( !_save_aligned(stream, &header, sizeof(header)) ) {
		i_error("sieve: failed to save binary header: %m");
		return FALSE;
	} 
	
	/* Create block containing all used extensions 
	 *   FIXME: Per-extension this should also store binary version numbers and 
	 *   the id of its first extension-specific block (if any)
	 */
	sieve_binary_block_set_active(sbin, SBIN_SYSBLOCK_EXTENSIONS);	
	ext_count = array_count(&sbin->extensions);
	sieve_binary_emit_integer(sbin, ext_count);
	
	for ( i = 0; i < ext_count; i++ ) {
		struct sieve_binary_extension * const *ext
			= array_idx(&sbin->extensions, i);
		
		sieve_binary_emit_cstring(sbin, (*ext)->extension->name);
	}
	sieve_binary_block_set_active(sbin, SBIN_SYSBLOCK_MAIN_PROGRAM);	
	
	for ( i = 0; i < sieve_binary_block_count(sbin); i++ ) {
		if ( !_save_block(sbin, stream, i) ) 
			return FALSE;
	}

	return TRUE;
} 

bool sieve_binary_save
	(struct sieve_binary *sbin, const char *path)
{
	bool result = TRUE;
	const char *temp_path;
	struct ostream *stream;
	int fd;
	
	/* Open it as temp file first, as not to overwrite an existing just yet */
	temp_path = t_strconcat(path, ".tmp", NULL);
	fd = open(temp_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	if ( fd < 0 ) {
		i_error("sieve: open(%s) failed for binary save: %m", temp_path);
		return FALSE;
	}

	stream = o_stream_create_fd(fd, 0, FALSE);
	result = _sieve_binary_save(sbin, stream);
	o_stream_destroy(&stream);
 
	if (close(fd) < 0)
		i_error("sieve: close() failed for binary save: %m");

	/* Replace any original binary atomically */
	if (result && (rename(temp_path, path) < 0)) {
		i_error("sieve: rename(%s, %s) failed for binary save: %m",
			temp_path, path);
		result = FALSE;
	}

	if ( !result ) {
		/* Get rid of temp output (if any) */
		(void) unlink(temp_path);
	}
	
	return result;
}

static inline const void *load_aligned_data
	(struct sieve_binary *sbin, const void **offset, size_t size)
{	
	*offset = SIEVE_BINARY_ALIGN_PTR(*offset);

	if ( PTR_OFFSET(*offset, size) <= sbin->memory_end ) {
		const void *data = *offset;
		*offset = PTR_OFFSET(*offset, size);
		
		return data;
	}
	
	return NULL;
}

#define LOAD_ALIGNED(sbin, offset, type) \
	(type *) load_aligned_data(sbin, offset, sizeof(type))

static buffer_t *_load_block
	(struct sieve_binary *sbin, const void **offset, unsigned int id)
{
	const struct sieve_binary_block_header *header = 
		LOAD_ALIGNED(sbin, offset, const struct sieve_binary_block_header);
	const void *data;
	buffer_t *block;
	
	if ( header == NULL ) {
		i_error("sieve: block %d of loaded binary %s is truncated", id, sbin->path);
		return NULL;
	}
	
	if ( header->id != id ) {
		i_error("sieve: block %d of loaded binary %s has unexpected id", id, 
			sbin->path);
		return NULL;
	}
	
	data = load_aligned_data(sbin, offset, header->size);
	if ( data == NULL ) {
		i_error("sieve: block %d of loaded binary %s has invalid size %d", 
			id, sbin->path, header->size);
		return NULL;
	}
	
	block = buffer_create_const_data(sbin->pool, data, header->size);
	sieve_binary_block_add(sbin, block);
	return block;
}

static bool _sieve_binary_load_extensions(struct sieve_binary *sbin)
{
	sieve_size_t offset = 0;
	sieve_size_t count = 0;
	bool result = TRUE;
	unsigned int i;
	
	sieve_binary_block_set_active(sbin, SBIN_SYSBLOCK_EXTENSIONS);

	if ( !sieve_binary_read_integer(sbin, &offset, &count) )
		return FALSE;
	
	for ( i = 0; result && i < count; i++ ) {
		T_FRAME(
			string_t *extension;
			int ext_id;
			
			if ( sieve_binary_read_string(sbin, &offset, &extension) ) { 
				printf("EXTENSION: %s\n", str_c(extension));
				
				ext_id = sieve_extension_get_by_name(str_c(extension), NULL);	
			
				if ( ext_id < 0 ) { 
					i_error("sieve: loaded binary %s contains unknown extension %s", 
						sbin->path, str_c(extension));
					result = FALSE;					
				} else 
					(void) sieve_binary_extension_link(sbin, ext_id);
			}	else
				result = FALSE;
		);
	}		
		
	return result;
}

static bool _sieve_binary_load(struct sieve_binary *sbin)
{
	const void *offset = sbin->memory;
	const struct sieve_binary_header *header;
	buffer_t *extensions;
	unsigned int i;
	
	/* Verify header */
	
	header = LOAD_ALIGNED(sbin, &offset, const struct sieve_binary_header);
	if ( header == NULL ) {
		i_error("sieve: loaded binary %s is not even large enough "
			"to contain a header.", sbin->path);
		return FALSE;
	}
	
	if ( header->magic != SIEVE_BINARY_MAGIC ) {
		if ( header->magic != SIEVE_BINARY_MAGIC_OTHER_ENDIAN ) 
			i_error("sieve: loaded binary %s has corrupted header %08x", 
				sbin->path, header->magic);

		return FALSE;
	}
	
	if ( header->version_major != SIEVE_BINARY_VERSION_MAJOR || 
		header->version_minor != SIEVE_BINARY_VERSION_MINOR ) {
		/* Binary is of different version. Caller will have to recompile */
		return FALSE;
	} 
	
	if ( header->blocks == 0 ) {
		i_error("sieve: loaded binary %s contains no blocks", sbin->path);
		return FALSE; 
	}
	
	/* Load extensions used by this binary */
	
	extensions =_load_block(sbin, &offset, 0);
	if ( extensions == NULL ) 
		return FALSE;
		
	if ( !_sieve_binary_load_extensions(sbin) ) {
		i_error("sieve: extension block of loaded binary %s is corrupt", 
			sbin->path);
		return FALSE;
	}	
	
	/* Load the other blocks */
	
	printf("BLOCKS: %d\n", header->blocks);
	
	for ( i = 1; i < header->blocks; i++ ) {	
		buffer_t *block = _load_block(sbin, &offset, i);
		if ( block == NULL ) {
			i_error("sieve: block %d of loaded binary %s is corrupt", 
				i, sbin->path);
			return FALSE;
		}
	}
				
	return TRUE;
}

struct sieve_binary *sieve_binary_load
	(const char *path, struct sieve_script *script)
{
	int fd;
	struct stat st;
	struct sieve_binary *sbin;
	size_t size;
	ssize_t ret;
	void *indata;
	
	if ( stat(path, &st) < 0 ) {
		if ( errno != ENOENT ) {
			i_error("sieve: binary stat(%s) failed: %m", path);
		}
		return NULL;
	}
	
	if ( (fd=open(path, O_RDONLY)) < 0 ) {
		if ( errno != ENOENT ) {
			i_error("sieve: binary open(%s) failed: %m", path);
		}
		return NULL;
	}
	
	/* Create binary object */
	sbin = sieve_binary_create(script);
	sbin->path = p_strdup(sbin->pool, path);
	
	/* Allocate memory buffer
	 * FIXME: provide mmap support 
	 */
	indata = p_malloc(sbin->pool, st.st_size);
	size = st.st_size; 
	
	sbin->memory = indata;
	sbin->memory_end = PTR_OFFSET(sbin->memory, st.st_size);

	/* Read the whole file into memory */
	while (size > 0) {
		if ( (ret=read(fd, indata, size)) <= 0 ) {
			if ( ret < 0 ) 
				i_error("sieve: failed to read from binary %s: %m", path);
			else
				i_error("sieve: binary %s was truncated: %m", path); 
			break;
		}
		
		indata = PTR_OFFSET(indata, ret);
		size -= ret;
	}	

	if ( size != 0 ) {
		/* Failed to read the whole file */
		sieve_binary_unref(&sbin);
		sbin = NULL;
	}
	
	if ( !_sieve_binary_load(sbin) ) {
		/* Failed to interpret binary header and/or block structure */
		sieve_binary_unref(&sbin);
		sbin = NULL;
	}
	
	if ( sbin != NULL )
		sieve_binary_activate(sbin);

	return sbin;
}

void sieve_binary_activate(struct sieve_binary *sbin)
{
	unsigned int i;
	
	sieve_binary_block_set_active(sbin, SBIN_SYSBLOCK_MAIN_PROGRAM);
	
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

	if ( ext != NULL && sieve_binary_extension_get_index(sbin, ext_id) == -1 ) {
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
	
	if ( ext_id > 0 && ext_id < (int) array_count(&sbin->extension_index) ) {
		ext = array_idx(&sbin->extension_index, (unsigned int) ext_id);
	
		if ( *ext != NULL )
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

inline sieve_size_t sieve_binary_emit_data
(struct sieve_binary *binary, void *data, sieve_size_t size) 
{
	sieve_size_t address = buffer_get_used_size(binary->data);
	  
	buffer_append(binary->data, data, size);
	
	return address;
}

inline sieve_size_t sieve_binary_emit_byte
(struct sieve_binary *binary, unsigned char byte) 
{
	return sieve_binary_emit_data(binary, &byte, 1);
}

inline void sieve_binary_update_data
(struct sieve_binary *binary, sieve_size_t address, void *data, 
	sieve_size_t size) 
{
	buffer_write(binary->data, address, data, size);
}

/* Offset emission functions */

/* FIXME: This is endian/alignment independent, but it is bound to be slow.
 */
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

/* FIXME: This is endian/alignment independent and it saves bytes, but it is 
 * bound to be slow.
 */
sieve_size_t sieve_binary_emit_integer
(struct sieve_binary *binary, sieve_size_t integer)
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

static inline sieve_size_t sieve_binary_emit_dynamic_data
	(struct sieve_binary *binary, void *data, size_t size)
{
	sieve_size_t address = sieve_binary_emit_integer(binary, size);
  (void) sieve_binary_emit_data(binary, data, size);
  
  return address;
}

inline sieve_size_t sieve_binary_emit_cstring
	(struct sieve_binary *binary, const char *str)
{
  sieve_size_t address = sieve_binary_emit_dynamic_data
  	(binary, (void *) str, strlen(str));
  sieve_binary_emit_byte(binary, 0);
  
  return address;
}

sieve_size_t sieve_binary_emit_string
	(struct sieve_binary *binary, const string_t *str)
{
  sieve_size_t address = sieve_binary_emit_dynamic_data
  	(binary, (void *) str_data(str), str_len(str));
	sieve_binary_emit_byte(binary, 0);
	
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
	
	*byte_val = 0;
	return FALSE;
}

bool sieve_binary_read_code
	(struct sieve_binary *binary, sieve_size_t *address, int *code) 
{	
	if ( ADDR_BYTES_LEFT(binary, address) >= 1 ) {
		if ( code != NULL )
			*code = ADDR_CODE_AT(binary, address);
		ADDR_JUMP(address, 1);
			
		return TRUE;
	}
	
	*code = 0;
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
	
	if ( ADDR_CODE_AT(binary, address) != 0 )
		return FALSE;
	
	ADDR_JUMP(address, 1);
  
	return TRUE;
}

/* Binary registry */

struct sieve_binary_registry {
	ARRAY_DEFINE(objects, const void *); 
};

static inline struct sieve_binary_registry *
	get_binary_registry(struct sieve_binary *sbin, int ext_id)
{
	return (struct sieve_binary_registry *) 
		sieve_binary_extension_get_context(sbin, ext_id);
}

const void *sieve_binary_registry_get_object
	(struct sieve_binary *sbin, int ext_id, int id)
{
	struct sieve_binary_registry *reg = get_binary_registry(sbin, ext_id);
	
	if ( (reg != NULL) && (id > 0) && 
		(id < (int) array_count(&reg->objects)) ) {
		const void * const *obj;

		obj = array_idx(&reg->objects, (unsigned int) id);

		return *obj;
	}
	
	return NULL;
}

void sieve_binary_registry_set_object
	(struct sieve_binary *sbin, int ext_id, int id, const void *object)
{
	struct sieve_binary_registry *reg = get_binary_registry(sbin, ext_id);

	array_idx_set(&reg->objects, (unsigned int) id, &object);
}

void sieve_binary_registry_init(struct sieve_binary *sbin, int ext_id)
{
	pool_t pool = sieve_binary_pool(sbin);
	
	struct sieve_binary_registry *reg = 
		p_new(pool, struct sieve_binary_registry, 1);
	
	/* Setup match-type registry */
	p_array_init(&reg->objects, pool, 4);

	sieve_binary_extension_set_context(sbin, ext_id, (void *) reg);
}

