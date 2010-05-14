/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "str-sanitize.h"
#include "mempool.h"
#include "buffer.h"
#include "hash.h"
#include "array.h"
#include "ostream.h"
#include "eacces-error.h"	
#include "safe-mkstemp.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-script.h"

#include "sieve-binary-private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/*
 * Macros
 */

#define SIEVE_BINARY_MAGIC              0xcafebabe
#define SIEVE_BINARY_MAGIC_OTHER_ENDIAN 0xbebafeca 

#define SIEVE_BINARY_ALIGN(offset) \
	(((offset) + 3) & ~3)
#define SIEVE_BINARY_ALIGN_PTR(ptr) \
	((void *) SIEVE_BINARY_ALIGN(((size_t) ptr)))

/*
 * Header and record structures of the binary on disk 
 */
 
struct sieve_binary_header {
	uint32_t magic;
	uint16_t version_major;
	uint16_t version_minor;
	uint32_t blocks;
};

struct sieve_binary_block_index {
	uint32_t id;
	uint32_t size;
	uint32_t offset;
	uint32_t ext_id;
};

struct sieve_binary_block_header {
	uint32_t id; 
	uint32_t size;
};

/* 
 * Saving the binary to a file. 
 */

static inline bool _save_skip(struct ostream *stream, size_t size)
{	
	if ( (o_stream_seek(stream, stream->offset + size)) <= 0 ) 
		return FALSE;
		
	return TRUE;
}

static inline bool _save_skip_aligned
(struct ostream *stream, size_t size, uoff_t *offset)
{
	uoff_t aligned_offset = SIEVE_BINARY_ALIGN(stream->offset);
	
	if ( (o_stream_seek(stream, aligned_offset + size)) <= 0 ) 
		return FALSE;
		
	if ( offset != NULL )
		*offset = aligned_offset;
		
	return TRUE;
}

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

static bool _save_aligned
(struct ostream *stream, const void *data, size_t size, uoff_t *offset)
{	
	uoff_t aligned_offset = SIEVE_BINARY_ALIGN(stream->offset);

	o_stream_cork(stream);
	
	/* Align the data by adding zeroes to the output stream */
	if ( stream->offset < aligned_offset ) {
		if ( !_save_skip(stream, aligned_offset - stream->offset) ) 
			return FALSE;
	}
	
	if ( !_save_full(stream, data, size) )
		return FALSE;
	
	o_stream_uncork(stream); 

	if ( offset != NULL )
		*offset = aligned_offset;

	return TRUE;
} 

static bool _save_block
(struct sieve_binary *sbin, struct ostream *stream, unsigned int id)
{
	struct sieve_binary_block_header block_header;
	struct sieve_binary_block *block;
	const void *data;
	size_t size;
		
	block = sieve_binary_block_get(sbin, id);
	if ( block == NULL )
		return FALSE;
		
	data = buffer_get_data(block->data, &size);
	
	block_header.id = id;
	block_header.size = size;
	
	if ( !_save_aligned(stream, &block_header,
		sizeof(block_header), &block->offset) )
		return FALSE;
	
	return _save_aligned(stream, data, size, NULL);
}

static bool _save_block_index_record
(struct sieve_binary *sbin, struct ostream *stream, unsigned int id)
{
	struct sieve_binary_block *block;
	struct sieve_binary_block_index header;
	
	block = sieve_binary_block_get(sbin, id);
	if ( block == NULL )
		return FALSE;
	
	header.id = id;
	header.size = buffer_get_used_size(block->data);
	header.ext_id = block->ext_index;
	header.offset = block->offset;
	
	if ( !_save_full(stream, &header, sizeof(header)) ) {
		sieve_sys_error("failed to save block index header %d: %m", id);
		
		return FALSE;
	}
	
	return TRUE;
}

static bool _sieve_binary_save
(struct sieve_binary *sbin, struct ostream *stream)
{
	struct sieve_binary_header header;
	struct sieve_binary_extension_reg *const *regs;
	struct sieve_binary_block *ext_block;
	unsigned int ext_count, blk_count, i;
	uoff_t block_index;
	
	blk_count = sieve_binary_block_count(sbin);
	
	/* Signal all extensions to finish generating their blocks */
	
	regs = array_get(&sbin->extensions, &ext_count);	
	for ( i = 0; i < ext_count; i++ ) {
		const struct sieve_binary_extension *binext = regs[i]->binext;
		
		if ( binext != NULL && binext->binary_save != NULL )
			binext->binary_save(regs[i]->extension, sbin, regs[i]->context);
	}
		
	/* Create header */
	
	header.magic = SIEVE_BINARY_MAGIC;
	header.version_major = SIEVE_BINARY_VERSION_MAJOR;
	header.version_minor = SIEVE_BINARY_VERSION_MINOR;
	header.blocks = blk_count;

	if ( !_save_aligned(stream, &header, sizeof(header), NULL) ) {
		sieve_sys_error("failed to save binary header: %m");
		return FALSE;
	} 
	
	/* Skip block index for now */
	
	if ( !_save_skip_aligned(stream, 
		sizeof(struct sieve_binary_block_index) * blk_count, &block_index) )
		return FALSE;
	
	/* Create block containing all used extensions 
	 *   FIXME: Per-extension this should also store binary version numbers.
	 */
	ext_block = sieve_binary_block_get(sbin, SBIN_SYSBLOCK_EXTENSIONS);
	i_assert( ext_block != NULL );
		
	ext_count = array_count(&sbin->linked_extensions);
	sieve_binary_emit_unsigned(ext_block, ext_count);
	
	for ( i = 0; i < ext_count; i++ ) {
		struct sieve_binary_extension_reg * const *ext
			= array_idx(&sbin->linked_extensions, i);
		
		sieve_binary_emit_cstring
			(ext_block, sieve_extension_name((*ext)->extension));
		sieve_binary_emit_unsigned(ext_block, (*ext)->block_id);
	}
		
	/* Save all blocks into the binary */
	
	for ( i = 0; i < blk_count; i++ ) {
		if ( !_save_block(sbin, stream, i) ) 
			return FALSE;
	}
	
	/* Create the block index */
	o_stream_seek(stream, block_index);
	for ( i = 0; i < blk_count; i++ ) {
		if ( !_save_block_index_record(sbin, stream, i) ) 
			return FALSE;
	}

	return TRUE;
} 

bool sieve_binary_save
(struct sieve_binary *sbin, const char *path)
{
	bool result = TRUE;
	string_t *temp_path;
	struct ostream *stream;
	int fd;
	mode_t save_mode =
		sbin->script == NULL ? 0600 : sieve_script_permissions(sbin->script);
	
	/* Use default path if none is specified */
	if ( path == NULL ) {
		if ( sbin->script == NULL ) {
			sieve_sys_error("cannot determine default binary save path "
				"with missing script object");
        	return FALSE;
		}
		path = sieve_script_binpath(sbin->script);
	}

	/* Open it as temp file first, as not to overwrite an existing just yet */
	temp_path = t_str_new(256);
	str_append(temp_path, path);
	fd = safe_mkstemp_hostpid(temp_path, save_mode, (uid_t)-1, (gid_t)-1);
	if ( fd < 0 ) {
		if ( errno == EACCES ) {
			sieve_sys_error("failed to save binary temporary file: %s",
				eacces_error_get_creating("open", str_c(temp_path)));
		} else {
			sieve_sys_error("failed to save binary temporary file: "
				"open(%s) failed: %m", str_c(temp_path));
		}
		return FALSE;
	}

	/* Save binary */
	stream = o_stream_create_fd(fd, 0, FALSE);
	result = _sieve_binary_save(sbin, stream);
	o_stream_destroy(&stream);

	/* Close saved binary */ 
	if ( close(fd) < 0 ) {
		sieve_sys_error("failed to close saved binary temporary file: "
			"close(fd=%s) failed: %m", str_c(temp_path));
	}

	/* Replace any original binary atomically */
	if ( result && (rename(str_c(temp_path), path) < 0) ) {
		if ( errno == EACCES ) {
			sieve_sys_error("failed to replace existing binary: %s", 
				eacces_error_get_creating("rename", path));			
		} else { 		
			sieve_sys_error("failed to replace existing binary: "
				"rename(%s, %s) failed: %m", str_c(temp_path), path);
		}
		result = FALSE;
	}

	if ( !result ) {
		/* Get rid of temp output (if any) */
		(void) unlink(str_c(temp_path));
	} else {
		if ( sbin->path == NULL || strcmp(sbin->path, path) != 0 ) {
			sbin->path = p_strdup(sbin->pool, path);
		}
	}
	
	return result;
}

/* 
 * Binary file management 
 */

bool sieve_binary_file_open
	(struct sieve_binary_file *file, const char *path)
{
	int fd;
	struct stat st;
	
	if ( (fd=open(path, O_RDONLY)) < 0 ) {
		if ( errno != ENOENT ) {
			if ( errno == EACCES ) {
				sieve_sys_error("failed to open binary: %s", 
					eacces_error_get("open", path));			
			} else {
				sieve_sys_error("failed to open binary: "
					"open(%s) failed: %m", path);
			}
		}
		return FALSE;
	}

	if ( fstat(fd, &st) < 0 ) {
		if ( errno != ENOENT ) {
			sieve_sys_error("failed to open binary: "
				"fstat(fd=%s) failed: %m", path);
		}
		return FALSE;
	}

	if ( !S_ISREG(st.st_mode) ) {
		sieve_sys_error("binary %s is not a regular file", path);
		return FALSE;		
	}
	
	file->fd = fd;
	file->st = st;

	return TRUE;
}
	
void sieve_binary_file_close(struct sieve_binary_file **file)
{
	if ( (*file)->fd != -1 ) {
		if ( close((*file)->fd) < 0 ) {
			sieve_sys_error("failed to close opened binary: "
				"close(fd=%s) failed: %m", (*file)->path);
		}
	}

	pool_unref(&(*file)->pool);
	
	*file = NULL;
}

#if 0 /* file_memory is currently unused */

/* File loaded/mapped to memory */

struct _file_memory {
	struct sieve_binary_file binfile;

	/* Pointer to the binary in memory */
	const void *memory;
	off_t memory_size;
};

static const void *_file_memory_load_data
	(struct sieve_binary_file *file, off_t *offset, size_t size)
{	
	struct _file_memory *fmem = (struct _file_memory *) file;

	*offset = SIEVE_BINARY_ALIGN(*offset);

	if ( (*offset) + size <= fmem->memory_size ) {
		const void *data = PTR_OFFSET(fmem->memory, *offset);
		*offset += size;
		file->offset = *offset;
		
		return data;
	}
		
	return NULL;
}

static buffer_t *_file_memory_load_buffer
	(struct sieve_binary_file *file, off_t *offset, size_t size)
{	
	struct _file_memory *fmem = (struct _file_memory *) file;

	*offset = SIEVE_BINARY_ALIGN(*offset);

	if ( (*offset) + size <= fmem->memory_size ) {
		const void *data = PTR_OFFSET(fmem->memory, *offset);
		*offset += size;
		file->offset = *offset;
		
		return buffer_create_const_data(file->pool, data, size);
	}
	
	return NULL;
}

static bool _file_memory_load(struct sieve_binary_file *file)
{
	struct _file_memory *fmem = (struct _file_memory *) file;
	int ret;
	size_t size;
	void *indata;
		
	i_assert(file->fd > 0);
		
	/* Allocate memory buffer
	 */
	indata = p_malloc(file->pool, file->st.st_size);
	size = file->st.st_size; 
	
	file->offset = 0; 
	fmem->memory = indata;
	fmem->memory_size = file->st.st_size;

	/* Return to beginning of the file */
	if ( lseek(file->fd, 0, SEEK_SET) == (off_t) -1 ) {
		sieve_sys_error("failed to seek() in binary %s: %m", file->path);
		return FALSE;
	}	

	/* Read the whole file into memory */
	while (size > 0) {
		if ( (ret=read(file->fd, indata, size)) <= 0 ) {
			sieve_sys_error("failed to read from binary %s: %m", file->path);
			break;
		}
		
		indata = PTR_OFFSET(indata, ret);
		size -= ret;
	}	

	if ( size != 0 ) {
		/* Failed to read the whole file */
		return FALSE;
	}
	
	return TRUE;
}

static struct sieve_binary_file *_file_memory_open(const char *path)
{
	pool_t pool;
	struct _file_memory *file;
	
	pool = pool_alloconly_create("sieve_binary_file_memory", 1024);
	file = p_new(pool, struct _file_memory, 1);
	file->binfile.pool = pool;
	file->binfile.path = p_strdup(pool, path);
	file->binfile.load = _file_memory_load;
	file->binfile.load_data = _file_memory_load_data;
	file->binfile.load_buffer = _file_memory_load_buffer;
	
	if ( !sieve_binary_file_open(&file->binfile, path) ) {
		pool_unref(&pool);
		return NULL;
	}

	return &file->binfile;
}

#endif /* file_memory is currently unused */

/* File open in lazy mode (only read what is needed into memory) */

static bool _file_lazy_read
(struct sieve_binary_file *file, off_t *offset, void *buffer, size_t size)
{
	int ret;
	void *indata = buffer;
	size_t insize = size;
	
	*offset = SIEVE_BINARY_ALIGN(*offset);
	
	/* Seek to the correct position */ 
	if ( *offset != file->offset && 
		lseek(file->fd, *offset, SEEK_SET) == (off_t) -1 ) {
		sieve_sys_error("failed to seek(fd, %lld, SEEK_SET) in binary %s: %m", 
			(long long) *offset, file->path);
		return FALSE;
	}	

	/* Read record into memory */
	while (insize > 0) {
		if ( (ret=read(file->fd, indata, insize)) <= 0 ) {
			if ( ret == 0 ) 
				sieve_sys_error("binary %s is truncated (more data expected)", 
					file->path);
			else
				sieve_sys_error("failed to read from binary %s: %m", file->path);
			break;
		}
		
		indata = PTR_OFFSET(indata, ret);
		insize -= ret;
	}	

	if ( insize != 0 ) {
		/* Failed to read the whole requested record */
		return FALSE;
	}
	
	*offset += size;
	file->offset = *offset;

	return TRUE;
}

static const void *_file_lazy_load_data
(struct sieve_binary_file *file, off_t *offset, size_t size)
{	
	void *data = t_malloc(size);

	if ( _file_lazy_read(file, offset, data, size) ) {
		return data;
	}
	
	return NULL;
}

static buffer_t *_file_lazy_load_buffer
(struct sieve_binary_file *file, off_t *offset, size_t size)
{			
	buffer_t *buffer = buffer_create_dynamic(file->pool, size);
	
	if ( _file_lazy_read
		(file, offset, buffer_get_space_unsafe(buffer, 0, size), size) ) {
		return buffer;
	}
	
	return NULL;
}

static struct sieve_binary_file *_file_lazy_open(const char *path)
{
	pool_t pool;
	struct sieve_binary_file *file;
	
	pool = pool_alloconly_create("sieve_binary_file_lazy", 4096);
	file = p_new(pool, struct sieve_binary_file, 1);
	file->pool = pool;
	file->path = p_strdup(pool, path);
	file->load_data = _file_lazy_load_data;
	file->load_buffer = _file_lazy_load_buffer;
	
	if ( !sieve_binary_file_open(file, path) ) {
		pool_unref(&pool);
		return NULL;
	}

	return file;
}

/* 
 * Load binary from a file
 */

#define LOAD_HEADER(sbin, offset, header) \
	(header *) sbin->file->load_data(sbin->file, offset, sizeof(header))

bool sieve_binary_load_block
(struct sieve_binary_block *sblock)
{
	struct sieve_binary *sbin = sblock->sbin;
	unsigned int id = sblock->id;
	off_t offset = sblock->offset;
	const struct sieve_binary_block_header *header = 
		LOAD_HEADER(sbin, &offset, const struct sieve_binary_block_header);
		
	if ( header == NULL ) {
		sieve_sys_error
			("block %d of loaded binary %s is truncated", id, sbin->path);
		return FALSE;
	}
	
	if ( header->id != id ) {
		sieve_sys_error("block %d of loaded binary %s has unexpected id %d", id, 
			sbin->path, header->id);
		return FALSE;
	}
	
	sblock->data = sbin->file->load_buffer(sbin->file, &offset, header->size);
	if ( sblock->data == NULL ) {
		sieve_sys_error("block %d of loaded binary %s has invalid size %d",
			id, sbin->path, header->size);
		return FALSE;
	}
		
	return TRUE;
}

static bool _load_block_index_record
(struct sieve_binary *sbin, off_t *offset, unsigned int id)
{
	const struct sieve_binary_block_index *record = 
		LOAD_HEADER(sbin, offset, const struct sieve_binary_block_index);
	struct sieve_binary_block *block;
	
	if ( record == NULL ) {
		sieve_sys_error("failed to read index record for block %d in binary %s", 
			id, sbin->path);
		return FALSE;
	}
	
	if ( record->id != id ) {
		sieve_sys_error("block index record %d of loaded binary %s "
			"has unexpected id %d", id, sbin->path, record->id);
		return FALSE;
	}
	
	block = sieve_binary_block_create_id(sbin, id);
	block->ext_index = record->ext_id;
	block->offset = record->offset;
	
	return TRUE;
}

static bool _sieve_binary_load_extensions(struct sieve_binary_block *sblock)
{
	struct sieve_binary *sbin = sblock->sbin;
	sieve_size_t offset = 0;
	unsigned int i, count;
	bool result = TRUE;
	
	if ( !sieve_binary_read_unsigned(sblock, &offset, &count) )
		return FALSE;
	
	for ( i = 0; result && i < count; i++ ) {
		T_BEGIN {
			string_t *extension;
			const struct sieve_extension *ext;
			
			if ( sieve_binary_read_string(sblock, &offset, &extension) ) { 
				ext = sieve_extension_get_by_name(sbin->svinst, str_c(extension));	
			
				if ( ext == NULL ) { 
					sieve_sys_error("loaded binary %s requires unknown extension '%s'", 
						sbin->path, str_sanitize(str_c(extension), 128));
					result = FALSE;					
				} else {
					struct sieve_binary_extension_reg *ereg = NULL;
					
					(void) sieve_binary_extension_register(sbin, ext, &ereg);
					if ( !sieve_binary_read_unsigned(sblock, &offset, &ereg->block_id) )
						result = FALSE;
				}
			}	else
				result = FALSE;
		} T_END;
	}		
		
	return result;
}

static bool _sieve_binary_open(struct sieve_binary *sbin)
{
	bool result = TRUE;
	off_t offset = 0;
	const struct sieve_binary_header *header;
	struct sieve_binary_block *ext_block;
	unsigned int i, blk_count;
	
	/* Verify header */
	
	T_BEGIN {
		header = LOAD_HEADER(sbin, &offset, const struct sieve_binary_header);
		if ( header == NULL ) {
			sieve_sys_error("opened binary %s is not even large enough "
				"to contain a header.", sbin->path);
			result = FALSE;

		} else if ( header->magic != SIEVE_BINARY_MAGIC ) {
			if ( header->magic != SIEVE_BINARY_MAGIC_OTHER_ENDIAN ) 
				sieve_sys_error("opened binary %s has corrupted header (0x%08x)", 
					sbin->path, header->magic);
			result = FALSE;

		} else if ( result && (
		  header->version_major != SIEVE_BINARY_VERSION_MAJOR || 
			header->version_minor != SIEVE_BINARY_VERSION_MINOR ) ) {

			/* Binary is of different version. Caller will have to recompile */
			result = FALSE;

		} else if ( result && header->blocks == 0 ) {
			sieve_sys_error("opened binary %s contains no blocks", sbin->path);
			result = FALSE; 

		} else {
			blk_count = header->blocks;
		}
	} T_END;
	
	if ( !result ) return FALSE;
	
	/* Load block index */
	
	for ( i = 0; i < blk_count && result; i++ ) {	
		T_BEGIN {
			if ( !_load_block_index_record(sbin, &offset, i) ) {
				sieve_sys_error(
					"block index record %d of opened binary %s is corrupt", 
					i, sbin->path);
				result = FALSE;
			}
		} T_END;
	}
	
	if ( !result ) return FALSE;
	
	/* Load extensions used by this binary */
	
	T_BEGIN {
		ext_block = sieve_binary_block_get(sbin, SBIN_SYSBLOCK_EXTENSIONS);
		if ( ext_block == NULL ) {
			result = FALSE;
		} else {
			if ( !_sieve_binary_load_extensions(ext_block) ) {
				sieve_sys_error("extension block of opened binary %s is corrupt", 
					sbin->path);
				result = FALSE;
			}
		}
	} T_END;
		
	return result;
}

static bool _sieve_binary_load(struct sieve_binary *sbin) 
{	
	bool result = TRUE;
	unsigned int i, blk_count;
	
	blk_count = array_count(&sbin->blocks);
	if ( blk_count == 1 ) {
		/* Binary is empty */
		return TRUE;
	}	

	/* Load the other blocks */
	
	for ( i = 0; result && i < blk_count; i++ ) {	
		T_BEGIN {
			if ( sieve_binary_block_get(sbin, i) == NULL )
				result = FALSE;
		} T_END;
	}
				
	return result;
}

struct sieve_binary *sieve_binary_open
(struct sieve_instance *svinst, const char *path, struct sieve_script *script)
{
	struct sieve_binary_extension_reg *const *regs;
	unsigned int ext_count, i;
	struct sieve_binary *sbin;
	struct sieve_binary_file *file;
	
	i_assert( script == NULL || sieve_script_svinst(script) == svinst );
	
	//file = _file_memory_open(path);	
	file = _file_lazy_open(path);
	if ( file == NULL )
		return NULL;
		
	/* Create binary object */
	sbin = sieve_binary_create(svinst, script);
	sbin->path = p_strdup(sbin->pool, path);
	sbin->file = file;
	
	if ( !_sieve_binary_open(sbin) ) {
		sieve_binary_unref(&sbin);
		return NULL;
	}
	
	sieve_binary_activate(sbin);
	
	/* Signal open event to extensions */
	regs = array_get(&sbin->extensions, &ext_count);	
	for ( i = 0; i < ext_count; i++ ) {
		const struct sieve_binary_extension *binext = regs[i]->binext;
		
		if ( binext != NULL && binext->binary_open != NULL && 
			!binext->binary_open(regs[i]->extension, sbin, regs[i]->context) ) {
			/* Extension thinks its corrupt */
			sieve_binary_unref(&sbin);
			return NULL;
		}
	}	

	return sbin;
}

bool sieve_binary_load(struct sieve_binary *sbin)
{
	i_assert(sbin->file != NULL);

	/*
	if ( sbin->file->load != NULL && !sbin->file->load(sbin->file) )
		return FALSE;	*/		
	
	if ( !_sieve_binary_load(sbin) ) {
		/* Failed to interpret binary header and/or block structure */
		return FALSE;
	}
	
	return TRUE;
}
