/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
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

static inline bool
_save_skip(struct sieve_binary *sbin, struct ostream *stream, size_t size)
{
	if ((o_stream_seek(stream, stream->offset + size)) <= 0) {
		e_error(sbin->event, "save: "
			"failed to skip output stream to position "
			"%"PRIuUOFF_T": %s", stream->offset + size,
			strerror(stream->stream_errno));
		return FALSE;
	}

	return TRUE;
}

static inline bool
_save_skip_aligned(struct sieve_binary *sbin, struct ostream *stream,
		   size_t size, uoff_t *offset)
{
	uoff_t aligned_offset = SIEVE_BINARY_ALIGN(stream->offset);

	if ((o_stream_seek(stream, aligned_offset + size)) <= 0) {
		e_error(sbin->event, "save: "
			"failed to skip output stream to position "
			"%"PRIuUOFF_T": %s", aligned_offset + size,
			strerror(stream->stream_errno));
		return FALSE;
	}

	if (offset != NULL)
		*offset = aligned_offset;
	return TRUE;
}

/* FIXME: Is this even necessary for a file? */
static bool
_save_full(struct sieve_binary *sbin, struct ostream *stream,
	   const void *data, size_t size)
{
	size_t bytes_left = size;
	const void *pdata = data;

	while (bytes_left > 0) {
		ssize_t ret;

		if ((ret = o_stream_send(stream, pdata, bytes_left)) <= 0) {
			e_error(sbin->event, "save: "
				"failed to write %zu bytes "
				"to output stream: %s", bytes_left,
				strerror(stream->stream_errno));
			return FALSE;
		}

		pdata = PTR_OFFSET(pdata, ret);
		bytes_left -= ret;
	}

	return TRUE;
}

static bool
_save_aligned(struct sieve_binary *sbin, struct ostream *stream,
	      const void *data, size_t size, uoff_t *offset)
{
	uoff_t aligned_offset = SIEVE_BINARY_ALIGN(stream->offset);

	o_stream_cork(stream);

	/* Align the data by adding zeroes to the output stream */
	if (stream->offset < aligned_offset) {
		if (!_save_skip(sbin, stream,
				(aligned_offset - stream->offset)))
			return FALSE;
	}

	if (!_save_full(sbin, stream, data, size))
		return FALSE;

	o_stream_uncork(stream);

	if (offset != NULL)
		*offset = aligned_offset;
	return TRUE;
}

static bool
_save_block(struct sieve_binary *sbin, struct ostream *stream, unsigned int id)
{
	struct sieve_binary_block_header block_header;
	struct sieve_binary_block *block;
	const void *data;
	size_t size;

	block = sieve_binary_block_get(sbin, id);
	if (block == NULL)
		return FALSE;

	data = buffer_get_data(block->data, &size);

	block_header.id = id;
	block_header.size = size;

	if (!_save_aligned(sbin, stream, &block_header, sizeof(block_header),
			   &block->offset))
		return FALSE;

	return _save_aligned(sbin, stream, data, size, NULL);
}

static bool
_save_block_index_record(struct sieve_binary *sbin, struct ostream *stream,
			 unsigned int id)
{
	struct sieve_binary_block *block;
	struct sieve_binary_block_index header;

	block = sieve_binary_block_get(sbin, id);
	if (block == NULL)
		return FALSE;

	header.id = id;
	header.size = buffer_get_used_size(block->data);
	header.ext_id = block->ext_index;
	header.offset = block->offset;

	if (!_save_full(sbin, stream, &header, sizeof(header))) {
		e_error(sbin->event, "save: "
			"failed to save block index header %d", id);
		return FALSE;
	}

	return TRUE;
}

static bool
sieve_binary_save_to_stream(struct sieve_binary *sbin, struct ostream *stream)
{
	struct sieve_binary_header header;
	struct sieve_binary_block *ext_block;
	unsigned int ext_count, blk_count, i;
	uoff_t block_index;

	blk_count = sieve_binary_block_count(sbin);

	/* Create header */

	header.magic = SIEVE_BINARY_MAGIC;
	header.version_major = SIEVE_BINARY_VERSION_MAJOR;
	header.version_minor = SIEVE_BINARY_VERSION_MINOR;
	header.blocks = blk_count;

	if (!_save_aligned(sbin, stream, &header, sizeof(header), NULL)) {
		e_error(sbin->event, "save: failed to save header");
		return FALSE;
	}

	/* Skip block index for now */

	if (!_save_skip_aligned(
		sbin, stream,
		(sizeof(struct sieve_binary_block_index) * blk_count),
		&block_index))
		return FALSE;

	/* Create block containing all used extensions */

	ext_block = sieve_binary_block_get(sbin, SBIN_SYSBLOCK_EXTENSIONS);
	i_assert(ext_block != NULL);
	sieve_binary_block_clear(ext_block);

	ext_count = array_count(&sbin->linked_extensions);
	sieve_binary_emit_unsigned(ext_block, ext_count);

	for (i = 0; i < ext_count; i++) {
		struct sieve_binary_extension_reg * const *ext =
			array_idx(&sbin->linked_extensions, i);

		sieve_binary_emit_cstring(
			ext_block, sieve_extension_name((*ext)->extension));
		sieve_binary_emit_unsigned(
			ext_block, sieve_extension_version((*ext)->extension));
		sieve_binary_emit_unsigned(ext_block, (*ext)->block_id);
	}

	/* Save all blocks into the binary */

	for (i = 0; i < blk_count; i++) {
		if (!_save_block(sbin, stream, i))
			return FALSE;
	}

	/* Create the block index */
	o_stream_seek(stream, block_index);
	for (i = 0; i < blk_count; i++) {
		if (!_save_block_index_record(sbin, stream, i))
			return FALSE;
	}

	return TRUE;
}

static int
sieve_binary_do_save(struct sieve_binary *sbin, const char *path, bool update,
		     mode_t save_mode, enum sieve_error *error_r)
{
	int result, fd;
	string_t *temp_path;
	struct ostream *stream;
	struct sieve_binary_extension_reg *const *regs;
	unsigned int ext_count, i;

	if (error_r != NULL)
		*error_r = SIEVE_ERROR_NONE;

	/* Check whether saving is necessary */
	if (!update && sbin->path != NULL && strcmp(sbin->path, path) == 0) {
		e_debug(sbin->event, "save: "
			"not saving binary, because it is already stored");
		return 0;
	}

	/* Open it as temp file first, as not to overwrite an existing just yet */
	temp_path = t_str_new(256);
	str_append(temp_path, path);
	str_append_c(temp_path, '.');
	fd = safe_mkstemp_hostpid(temp_path, save_mode, (uid_t)-1, (gid_t)-1);
	if (fd < 0) {
		if (errno == EACCES) {
			e_error(sbin->event, "save: "
				"failed to create temporary file: %s",
				eacces_error_get_creating("open",
							  str_c(temp_path)));
			if (error_r != NULL)
				*error_r = SIEVE_ERROR_NO_PERMISSION;
		} else {
			e_error(sbin->event, "save: "
				"failed to create temporary file: "
				"open(%s) failed: %m", str_c(temp_path));
			if (error_r != NULL)
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
		}
		return -1;
	}

	/* Signal all extensions that we're about to save the binary */
	regs = array_get(&sbin->extensions, &ext_count);
	for (i = 0; i < ext_count; i++) {
		const struct sieve_binary_extension *binext = regs[i]->binext;

		if (binext != NULL && binext->binary_pre_save != NULL &&
		    !binext->binary_pre_save(regs[i]->extension, sbin,
					     regs[i]->context, error_r))
			return -1;
	}

	/* Save binary */
	result = 1;
	stream = o_stream_create_fd(fd, 0);
	if (!sieve_binary_save_to_stream(sbin, stream)) {
		result = -1;
		if (error_r != NULL)
			*error_r = SIEVE_ERROR_TEMP_FAILURE;
	}
	o_stream_destroy(&stream);

	/* Close saved binary */
	if (close(fd) < 0) {
		e_error(sbin->event, "save: "
			"failed to close temporary file: "
			"close(fd=%s) failed: %m", str_c(temp_path));
	}

	/* Replace any original binary atomically */
	if (result > 0 && (rename(str_c(temp_path), path) < 0)) {
		if (errno == EACCES) {
			e_error(sbin->event, "save: "
				"failed to save binary: %s",
				eacces_error_get_creating("rename", path));
			if (error_r != NULL)
				*error_r = SIEVE_ERROR_NO_PERMISSION;
		} else {
			e_error(sbin->event, "save: "
				"failed to save binary: "
				"rename(%s, %s) failed: %m",
				str_c(temp_path), path);
			if (error_r != NULL)
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
		}
		result = -1;
	}

	if (result < 0) {
		/* Get rid of temp output (if any) */
		if (unlink(str_c(temp_path)) < 0 && errno != ENOENT) {
			e_error(sbin->event, "save: "
				"failed to clean up after error: "
				"unlink(%s) failed: %m", str_c(temp_path));
		}
	} else {
		if (sbin->path == NULL)
			sbin->path = p_strdup(sbin->pool, path);

		/* Signal all extensions that we successfully saved the binary */
		regs = array_get(&sbin->extensions, &ext_count);
		for (i = 0; i < ext_count; i++) {
			const struct sieve_binary_extension *binext =
				regs[i]->binext;

			if (binext != NULL &&
			    binext->binary_post_save != NULL &&
			    !binext->binary_post_save(regs[i]->extension, sbin,
						      regs[i]->context,
						      error_r)) {
				result = -1;
				break;
			}
		}

		if (result < 0 && unlink(path) < 0 && errno != ENOENT) {
			e_error(sbin->event, "failed to clean up after error: "
				"unlink(%s) failed: %m", path);
		}
		sbin->path = NULL;
	}

	return result;
}

int sieve_binary_save(struct sieve_binary *sbin, const char *path, bool update,
		      mode_t save_mode, enum sieve_error *error_r)
{
	int ret;

	sieve_binary_update_event(sbin, path);
	ret = sieve_binary_do_save(sbin, path, update, save_mode, error_r);
	sieve_binary_update_event(sbin, NULL);

	return ret;
}


/*
 * Binary file management
 */

static bool
sieve_binary_file_open(struct sieve_binary_file *file,
		       struct sieve_binary *sbin, const char *path,
		       enum sieve_error *error_r)
{
	int fd;
	bool result = TRUE;
	struct stat st;

	if (error_r != NULL)
		*error_r = SIEVE_ERROR_NONE;

	if ((fd = open(path, O_RDONLY)) < 0) {
		switch (errno) {
		case ENOENT:
			if (error_r != NULL)
				*error_r = SIEVE_ERROR_NOT_FOUND;
			break;
		case EACCES:
			e_error(sbin->event, "open: "
				"failed to open: %s",
				eacces_error_get("open", path));
			if (error_r != NULL)
				*error_r = SIEVE_ERROR_NO_PERMISSION;
			break;
		default:
			e_error(sbin->event, "open: "
				"failed to open: open(%s) failed: %m", path);
			if (error_r != NULL)
				*error_r = SIEVE_ERROR_TEMP_FAILURE;
			break;
		}
		return FALSE;
	}

	if (fstat(fd, &st) < 0) {
		if (errno != ENOENT)
			e_error(sbin->event, "open: fstat() failed: %m");
		result = FALSE;
	}

	if (result && !S_ISREG(st.st_mode)) {
		e_error(sbin->event, "open: "
			"binary is not a regular file");
		result = FALSE;
	}

	if (!result)	{
		if (close(fd) < 0) {
			e_error(sbin->event, "open: "
				"close() failed after error: %m");
		}
		return FALSE;
	}

	file->sbin = sbin;
	file->fd = fd;
	file->st = st;

	return TRUE;
}

void sieve_binary_file_close(struct sieve_binary_file **_file)
{
	struct sieve_binary_file *file = *_file;
	struct sieve_binary *sbin = file->sbin;

	*_file = NULL;

	if (file->fd != -1) {
		if (close(file->fd) < 0) {
			e_error(sbin->event, "close: "
				"failed to close: close() failed: %m");
		}
	}

	pool_unref(&file->pool);
}

/* File open in lazy mode (only read what is needed into memory) */

static bool
_file_lazy_read(struct sieve_binary_file *file, off_t *offset,
		void *buffer, size_t size)
{
	struct sieve_binary *sbin = file->sbin;
	int ret;
	void *indata = buffer;
	size_t insize = size;

	*offset = SIEVE_BINARY_ALIGN(*offset);

	/* Seek to the correct position */
	if (*offset != file->offset &&
	    lseek(file->fd, *offset, SEEK_SET) == (off_t) -1) {
		e_error(sbin->event, "read: "
			"failed to seek(fd, %lld, SEEK_SET): %m",
			(long long) *offset);
		return FALSE;
	}

	/* Read record into memory */
	while (insize > 0) {
		if ((ret = read(file->fd, indata, insize)) <= 0) {
			if (ret == 0) {
				e_error(sbin->event, "read: "
					"binary is truncated "
					"(more data expected)");
			} else {
				e_error(sbin->event, "read: "
					"failed to read from binary: %m");
			}
			break;
		}

		indata = PTR_OFFSET(indata, ret);
		insize -= ret;
	}

	if (insize != 0) {
		/* Failed to read the whole requested record */
		return FALSE;
	}

	*offset += size;
	file->offset = *offset;

	return TRUE;
}

static const void *
_file_lazy_load_data(struct sieve_binary_file *file,
		     off_t *offset, size_t size)
{
	void *data = t_malloc_no0(size);

	if (_file_lazy_read(file, offset, data, size))
		return data;

	return NULL;
}

static buffer_t *
_file_lazy_load_buffer(struct sieve_binary_file *file,
		       off_t *offset, size_t size)
{
	buffer_t *buffer = buffer_create_dynamic(file->pool, size);

	if (_file_lazy_read(file, offset,
			    buffer_get_space_unsafe(buffer, 0, size), size))
		return buffer;

	return NULL;
}

static struct sieve_binary_file *
_file_lazy_open(struct sieve_binary *sbin, const char *path,
		enum sieve_error *error_r)
{
	pool_t pool;
	struct sieve_binary_file *file;

	pool = pool_alloconly_create("sieve_binary_file_lazy", 4096);
	file = p_new(pool, struct sieve_binary_file, 1);
	file->pool = pool;
	file->path = p_strdup(pool, path);
	file->load_data = _file_lazy_load_data;
	file->load_buffer = _file_lazy_load_buffer;

	if (!sieve_binary_file_open(file, sbin, path, error_r)) {
		pool_unref(&pool);
		return NULL;
	}

	return file;
}

/*
 * Load binary from a file
 */

#define LOAD_HEADER(sbin, offset, header) \
	(header *)sbin->file->load_data(sbin->file, offset, sizeof(header))

bool sieve_binary_load_block(struct sieve_binary_block *sblock)
{
	struct sieve_binary *sbin = sblock->sbin;
	unsigned int id = sblock->id;
	off_t offset = sblock->offset;
	const struct sieve_binary_block_header *header =
		LOAD_HEADER(sbin, &offset,
			    const struct sieve_binary_block_header);

	if (header == NULL) {
		e_error(sbin->event, "load: binary is corrupt: "
			"failed to read header of block %d", id);
		return FALSE;
	}

	if (header->id != id) {
		e_error(sbin->event, "load: binary is corrupt: "
			"header of block %d has non-matching id %d",
			id, header->id);
		return FALSE;
	}

	sblock->data = sbin->file->load_buffer(sbin->file, &offset, header->size);
	if (sblock->data == NULL) {
		e_error(sbin->event, "load: "
			"failed to read block %d of binary (size=%d)",
			id, header->size);
		return FALSE;
	}

	return TRUE;
}

static bool
_read_block_index_record(struct sieve_binary *sbin, off_t *offset,
			 unsigned int id)
{
	const struct sieve_binary_block_index *record =
		LOAD_HEADER(sbin, offset,
			    const struct sieve_binary_block_index);
	struct sieve_binary_block *block;

	if (record == NULL) {
		e_error(sbin->event, "open: binary is corrupt: "
			"failed to load block index record %d", id);
		return FALSE;
	}

	if (record->id != id) {
		e_error(sbin->event, "open: binary is corrupt: "
			"block index record %d has unexpected id %d",
			id, record->id);
		return FALSE;
	}

	block = sieve_binary_block_create_id(sbin, id);
	block->ext_index = record->ext_id;
	block->offset = record->offset;

	return TRUE;
}

static int _read_extensions(struct sieve_binary_block *sblock)
{
	struct sieve_binary *sbin = sblock->sbin;
	sieve_size_t offset = 0;
	unsigned int i, count;
	int result = 1;

	if (!sieve_binary_read_unsigned(sblock, &offset, &count))
		return -1;

	for (i = 0; result > 0 && i < count; i++) {
		T_BEGIN {
			string_t *extension;
			const struct sieve_extension *ext;
			unsigned int version;

			if (sieve_binary_read_string(sblock, &offset,
						     &extension)) {
				ext = sieve_extension_get_by_name(
					sbin->svinst, str_c(extension));

				if (ext == NULL) {
					e_error(sbin->event, "open: "
						"binary requires unknown extension `%s'",
						str_sanitize(str_c(extension), 128));
					result = 0;
				} else {
					struct sieve_binary_extension_reg *ereg = NULL;

					(void)sieve_binary_extension_register(sbin, ext, &ereg);
					if (!sieve_binary_read_unsigned(sblock, &offset, &version) ||
					    !sieve_binary_read_unsigned(sblock, &offset, &ereg->block_id)) {
						result = -1;
					} else if (!sieve_extension_version_is(ext, version)) {
						e_debug(sbin->event, "open: "
							"binary was compiled with different version "
							"of the `%s' extension (compiled v%d, expected v%d;"
							"automatically fixed when re-compiled)",
							sieve_extension_name(ext), version,
							sieve_extension_version(ext));
						result = 0;
					}
				}
			} else {
				result = -1;
			}
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
	int ret;

	/* Verify header */

	T_BEGIN {
		header = LOAD_HEADER(sbin, &offset,
				     const struct sieve_binary_header);
		/* Check header presence */
		if (header == NULL) {
			e_error(sbin->event, "open: "
				"file is not large enough to contain the header");
			result = FALSE;
		/* Check header validity */
		} else if (header->magic != SIEVE_BINARY_MAGIC) {
			if (header->magic != SIEVE_BINARY_MAGIC_OTHER_ENDIAN) {
				e_error(sbin->event, "open: "
					"binary has corrupted header "
					"(0x%08x) or it is not a Sieve binary",
					header->magic);
			} else {
				e_debug(sbin->event, "open: "
					"binary stored with in different endian format "
					"(automatically fixed when re-compiled)");
			}
			result = FALSE;
		/* Check binary version */
		} else if (result &&
			   (header->version_major != SIEVE_BINARY_VERSION_MAJOR ||
			    header->version_minor != SIEVE_BINARY_VERSION_MINOR)) {
			/* Binary is of different version. Caller will have to recompile */
			e_debug(sbin->event, "open: "
				"binary stored with different binary version %d.%d "
				"(!= %d.%d; automatically fixed when re-compiled)",
				(int) header->version_major, header->version_minor,
				SIEVE_BINARY_VERSION_MAJOR, SIEVE_BINARY_VERSION_MINOR);
			result = FALSE;
		/* Check block content */
		} else if (result && header->blocks == 0) {
			e_error(sbin->event, "open: binary is corrupt: "
				"it contains no blocks");
			result = FALSE;
		/* Valid */
		} else {
			blk_count = header->blocks;
		}
	} T_END;

	if (!result)
		return FALSE;

	/* Load block index */

	for (i = 0; i < blk_count && result; i++) {
		T_BEGIN {
			if (!_read_block_index_record(sbin, &offset, i))
				result = FALSE;
		} T_END;
	}

	if (!result)
		return FALSE;

	/* Load extensions used by this binary */

	T_BEGIN {
		ext_block = sieve_binary_block_get(
			sbin, SBIN_SYSBLOCK_EXTENSIONS);
		if (ext_block == NULL) {
			result = FALSE;
		} else if ((ret = _read_extensions(ext_block)) <= 0) {
			if (ret < 0) {
				e_error(sbin->event, "open: binary is corrupt: "
					"failed to load extension block");
			}
			result = FALSE;
		}
	} T_END;

	return result;
}

struct sieve_binary *
sieve_binary_open(struct sieve_instance *svinst, const char *path,
		  struct sieve_script *script, enum sieve_error *error_r)
{
	struct sieve_binary_extension_reg *const *regs;
	unsigned int ext_count, i;
	struct sieve_binary *sbin;
	struct sieve_binary_file *file;

	i_assert(script == NULL || sieve_script_svinst(script) == svinst);

	/* Create binary object */
	sbin = sieve_binary_create(svinst, script);
	sbin->path = p_strdup(sbin->pool, path);

	if ((file = _file_lazy_open(sbin, path, error_r)) == NULL) {
		sieve_binary_unref(&sbin);
		return NULL;
	}

	sbin->file = file;

	event_set_append_log_prefix(
		sbin->event,
		t_strdup_printf("binary %s: ", path));

	if (!_sieve_binary_open(sbin)) {
		sieve_binary_unref(&sbin);
		if (error_r != NULL)
			*error_r = SIEVE_ERROR_NOT_VALID;
		return NULL;
	}

	sieve_binary_activate(sbin);

	/* Signal open event to extensions */
	regs = array_get(&sbin->extensions, &ext_count);
	for (i = 0; i < ext_count; i++) {
		const struct sieve_binary_extension *binext = regs[i]->binext;

		if (binext != NULL && binext->binary_open != NULL &&
		    !binext->binary_open(regs[i]->extension, sbin,
					 regs[i]->context)) {
			/* Extension thinks its corrupt */
			if (error_r != NULL)
				*error_r = SIEVE_ERROR_NOT_VALID;
			sieve_binary_unref(&sbin);
			return NULL;
		}
	}
	return sbin;
}
