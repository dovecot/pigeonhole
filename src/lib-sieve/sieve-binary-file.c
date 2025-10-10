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
#include "file-lock.h"

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

#define SIEVE_BINARY_MAGIC                      0xcafebabe
#define SIEVE_BINARY_MAGIC_OTHER_ENDIAN         0xbebafeca

#define SIEVE_BINARY_ALIGN(offset) \
	(((offset) + 3) & ~3U)
#define SIEVE_BINARY_ALIGN_PTR(ptr) \
	((void *)SIEVE_BINARY_ALIGN(((size_t) ptr)))

#define SIEVE_BINARY_PRE_HDR_SIZE_MAJOR         1
#define SIEVE_BINARY_PRE_HDR_SIZE_MINOR         4
#define SIEVE_BINARY_PRE_HDR_SIZE_HDR_SIZE      12

/*
 * Header and record structures of the binary on disk
 */

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
 * Utility
 */

static bool sieve_binary_can_update(struct sieve_binary *sbin)
{
	const char *dirpath, *p;

	p = strrchr(sbin->path, '/');
	if (p == NULL)
		dirpath = ".";
	else
		dirpath = t_strdup_until(sbin->path, p);

	return (access(dirpath, W_OK | X_OK) == 0);
}

/*
 * Header manipulation
 */

static int
sieve_binary_file_read_header(struct sieve_binary *sbin, int fd,
			      struct sieve_binary_header *header_r,
			      enum sieve_error *error_code_r)
{
	struct sieve_binary_header header;
	ssize_t rret;

	sieve_error_args_init(&error_code_r, NULL);

	rret = pread(fd, &header, sizeof(header), 0);
	if (rret == 0) {
		e_error(sbin->event, "read: "
			"file is not large enough to contain the header");
		*error_code_r = SIEVE_ERROR_NOT_VALID;
		return -1;
	} else if (rret < 0) {
		e_error(sbin->event, "read: "
			"failed to read from binary: %m");
		*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	} else if (rret != sizeof(header)) {
		e_error(sbin->event, "read: "
			"header read only partially %zd/%zu",
			rret, sizeof(header));
		*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}

	/* Check header validity */
	if (header.magic != SIEVE_BINARY_MAGIC) {
		if (header.magic != SIEVE_BINARY_MAGIC_OTHER_ENDIAN) {
			e_error(sbin->event, "read: "
				"binary has corrupted header "
				"(0x%08x) or it is not a Sieve binary",
				header.magic);
		} else {
			e_error(sbin->event, "read: "
				"binary stored with in different endian format "
				"(automatically fixed when re-compiled)");
		}
		*error_code_r = SIEVE_ERROR_NOT_VALID;
		return -1;
	}
	/* Check binary version */
	if (header.version_major == SIEVE_BINARY_PRE_HDR_SIZE_MAJOR &&
	    header.version_minor == SIEVE_BINARY_PRE_HDR_SIZE_MINOR) {
		/* Old header without hdr_size; clear new fields */
		static const size_t old_header_size =
			SIEVE_BINARY_PRE_HDR_SIZE_HDR_SIZE;
		memset(PTR_OFFSET(&header, old_header_size), 0,
		       (sizeof(header) - old_header_size));
		header.hdr_size = old_header_size;
	} else if (header.version_major != SIEVE_BINARY_VERSION_MAJOR) {
		/* Binary is of different major version. Caller will have to
		   recompile */
		bool important = (sbin->script == NULL ||
				  !sieve_binary_can_update(sbin));
		enum log_type log_type = (important ?
					  LOG_TYPE_ERROR : LOG_TYPE_DEBUG);
		e_log(sbin->event, log_type, "read: "
		      "binary stored with different major version %d.%d "
		      "(!= %d.%d; automatically fixed when re-compiled)",
		      (int)header.version_major, (int)header.version_minor,
		      SIEVE_BINARY_VERSION_MAJOR, SIEVE_BINARY_VERSION_MINOR);
		*error_code_r = SIEVE_ERROR_NOT_VALID;
		return -1;
	} else if (header.hdr_size < SIEVE_BINARY_BASE_HEADER_SIZE) {
		/* Header size is smaller than base size */
		e_error(sbin->event, "read: "
			"binary is corrupt: header size is too small");
		*error_code_r = SIEVE_ERROR_NOT_VALID;
		return -1;
	}
	/* Check block content */
	if (header.blocks == 0) {
		e_error(sbin->event, "read: "
			"binary is corrupt: it contains no blocks");
		*error_code_r = SIEVE_ERROR_NOT_VALID;
		return -1;
	}
	/* Valid */
	*header_r = header;
	return 0;
}

static int
sieve_binary_file_write_header(struct sieve_binary *sbin, int fd,
			       struct sieve_binary_header *header,
			       enum sieve_error *error_code_r)
{
	ssize_t wret;

	wret = pwrite(fd, header, sizeof(*header), 0);
	if (wret < 0) {
		e_error(sbin->event, "update: "
			"failed to write to binary: %m");
		*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	} else if (wret != sizeof(*header)) {
		e_error(sbin->event, "update: "
			"header written partially %zd/%zu",
			wret, sizeof(*header));
		*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}
	return 0;
}

static void sieve_binary_file_update_header(struct sieve_binary *sbin)
{
	struct sieve_binary_header *header = &sbin->header;
	struct sieve_resource_usage rusage;

	sieve_binary_get_resource_usage(sbin, &rusage);

	i_zero(&header->resource_usage);
	if (HAS_ALL_BITS(header->flags, SIEVE_BINARY_FLAG_RESOURCE_LIMIT) ||
	    sieve_resource_usage_is_high(sbin->svinst, &rusage)) {
		header->resource_usage.update_time = ioloop_time;
		header->resource_usage.cpu_time_msecs = rusage.cpu_time_msecs;
	}

	sieve_resource_usage_init(&sbin->rusage);
	sbin->rusage_updated = FALSE;

	(void)sieve_binary_check_resource_usage(sbin);
}

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

		ret = o_stream_send(stream, pdata, bytes_left);
		if (ret <= 0) {
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
	struct sieve_binary_header *header = &sbin->header;
	struct sieve_binary_block *ext_block;
	unsigned int ext_count, blk_count, i;
	uoff_t block_index;

	blk_count = sieve_binary_block_count(sbin);

	/* Create header */

	header->magic = SIEVE_BINARY_MAGIC;
	header->version_major = SIEVE_BINARY_VERSION_MAJOR;
	header->version_minor = SIEVE_BINARY_VERSION_MINOR;
	header->blocks = blk_count;
	header->hdr_size = sizeof(*header);

	header->flags &= ENUM_NEGATE(SIEVE_BINARY_FLAG_RESOURCE_LIMIT);
	sieve_binary_file_update_header(sbin);

	if (!_save_aligned(sbin, stream, header, sizeof(*header), NULL)) {
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
		struct sieve_binary_extension_reg *const *ext =
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

	if (o_stream_finish(stream) <= 0) {
		e_error(sbin->event, "save: "
			"failed to finish output stream: %s",
			o_stream_get_error(stream));
		return FALSE;
	}
	return TRUE;
}

static int
sieve_binary_do_save(struct sieve_binary *sbin, const char *path, bool update,
		     mode_t save_mode, enum sieve_error *error_code_r)
{
	int result, fd;
	string_t *temp_path;
	struct ostream *stream;
	struct sieve_binary_extension_reg *const *regs;
	unsigned int ext_count, i;

	sieve_error_args_init(&error_code_r, NULL);

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
			*error_code_r = SIEVE_ERROR_NO_PERMISSION;
		} else {
			e_error(sbin->event, "save: "
				"failed to create temporary file: "
				"open(%s) failed: %m", str_c(temp_path));
			*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		}
		return -1;
	}

	/* Signal all extensions that we're about to save the binary */
	regs = array_get(&sbin->extensions, &ext_count);
	for (i = 0; i < ext_count; i++) {
		const struct sieve_binary_extension *binext = regs[i]->binext;

		if (binext != NULL && binext->binary_pre_save != NULL &&
		    !binext->binary_pre_save(regs[i]->extension, sbin,
					     regs[i]->context, error_code_r)) {
			i_assert(*error_code_r != SIEVE_ERROR_NONE);
			return -1;
		}
	}

	/* Save binary */
	result = 1;
	stream = o_stream_create_fd(fd, 0);
	if (!sieve_binary_save_to_stream(sbin, stream)) {
		result = -1;
		*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		o_stream_ignore_last_errors(stream);
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
			*error_code_r = SIEVE_ERROR_NO_PERMISSION;
		} else {
			e_error(sbin->event, "save: "
				"failed to save binary: "
				"rename(%s, %s) failed: %m",
				str_c(temp_path), path);
			*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
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

		/* Signal all extensions that we successfully saved the binary.
		 */
		regs = array_get(&sbin->extensions, &ext_count);
		for (i = 0; i < ext_count; i++) {
			const struct sieve_binary_extension *binext =
				regs[i]->binext;

			if (binext != NULL &&
			    binext->binary_post_save != NULL &&
			    !binext->binary_post_save(regs[i]->extension, sbin,
						      regs[i]->context,
						      error_code_r)) {
				i_assert(*error_code_r != SIEVE_ERROR_NONE);
				result = -1;
				break;
			}
		}

		if (result < 0 && unlink(path) < 0 && errno != ENOENT) {
			e_error(sbin->event, "failed to clean up after error: "
				"unlink(%s) failed: %m", path);
		}
	}

	return result;
}

int sieve_binary_save(struct sieve_binary *sbin, const char *path, bool update,
		      mode_t save_mode, enum sieve_error *error_code_r)
{
	int ret;

	sieve_binary_update_event(sbin, path);
	ret = sieve_binary_do_save(sbin, path, update, save_mode, error_code_r);
	sieve_binary_update_event(sbin, NULL);

	return ret;
}


/*
 * Binary file management
 */

static int
sieve_binary_fd_open(struct sieve_binary *sbin, const char *path,
		     int open_flags, enum sieve_error *error_code_r)
{
	int fd;

	fd = open(path, open_flags);
	if (fd < 0) {
		switch (errno) {
		case ENOENT:
			*error_code_r = SIEVE_ERROR_NOT_FOUND;
			break;
		case EACCES:
			e_error(sbin->event, "open: "
				"failed to open: %s",
				eacces_error_get("open", path));
			*error_code_r = SIEVE_ERROR_NO_PERMISSION;
			break;
		default:
			e_error(sbin->event, "open: "
				"failed to open: open(%s) failed: %m", path);
			*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
			break;
		}
		return -1;
	}
	return fd;
}

static int
sieve_binary_file_open(struct sieve_binary *sbin, const char *path,
		       struct sieve_binary_file **file_r,
		       enum sieve_error *error_code_r)
{
	int fd, ret = 0;
	struct stat st;

	sieve_error_args_init(&error_code_r, NULL);

	fd = sieve_binary_fd_open(sbin, path, O_RDONLY, error_code_r);
	if (fd < 0)
		return -1;

	if (fstat(fd, &st) < 0) {
		if (errno == ENOENT)
			*error_code_r = SIEVE_ERROR_NOT_FOUND;
		else {
			e_error(sbin->event, "open: fstat(%s) failed: %m",
				path);
			*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		}
		ret = -1;
	}

	if (ret == 0 && !S_ISREG(st.st_mode)) {
		e_error(sbin->event, "open: "
			"binary is not a regular file");
		*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		ret = -1;
	}

	if (ret < 0) {
		if (close(fd) < 0) {
			e_error(sbin->event, "open: "
				"close() failed after error: %m");
		}
		return -1;
	}

	pool_t pool;
	struct sieve_binary_file *file;

	pool = pool_alloconly_create("sieve_binary_file", 4096);
	file = p_new(pool, struct sieve_binary_file, 1);
	file->pool = pool;
	file->path = p_strdup(pool, path);
	file->fd = fd;
	file->st = st;
	file->sbin = sbin;

	*file_r = file;
	return 0;
}

void sieve_binary_file_close(struct sieve_binary_file **_file)
{
	struct sieve_binary_file *file = *_file;

	*_file = NULL;
	if (file == NULL)
		return;

	if (file->fd != -1) {
		if (close(file->fd) < 0) {
			e_error(file->sbin->event, "close: "
				"failed to close: close() failed: %m");
		}
	}

	pool_unref(&file->pool);
}

static int
sieve_binary_file_read(struct sieve_binary_file *file, off_t *offset,
		       void *buffer, size_t size)
{
	struct sieve_binary *sbin = file->sbin;
	int ret;
	void *indata = buffer;
	size_t insize = size;

	*offset = SIEVE_BINARY_ALIGN(*offset);

	/* Seek to the correct position */
	if (*offset != file->offset &&
	    lseek(file->fd, *offset, SEEK_SET) == (off_t)-1) {
		e_error(sbin->event, "read: "
			"failed to seek(fd, %lld, SEEK_SET): %m",
			(long long) *offset);
		return -1;
	}

	/* Read record into memory */
	while (insize > 0) {
		ret = read(file->fd, indata, insize);
		if (ret <= 0) {
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
		return 0;
	}

	*offset += size;
	file->offset = *offset;
	return 1;
}

static const void *
sieve_binary_file_load_data(struct sieve_binary_file *file,
			    off_t *offset, size_t size)
{
	void *data = t_malloc_no0(size);

	if (sieve_binary_file_read(file, offset, data, size) > 0)
		return data;

	return NULL;
}

static buffer_t *
sieve_binary_file_load_buffer(struct sieve_binary_file *file,
			      off_t *offset, size_t size)
{
	buffer_t *buffer = buffer_create_dynamic(file->pool, size);

	if (sieve_binary_file_read(file, offset,
				   buffer_get_space_unsafe(buffer, 0, size),
				   size) > 0)
		return buffer;

	return NULL;
}

/*
 * Load binary from a file
 */

#define LOAD_HEADER(sbin, offset, header) \
	(header *)sieve_binary_file_load_data(sbin->file, offset, \
					      sizeof(header))

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

	sblock->data = sieve_binary_file_load_buffer(sbin->file, &offset,
						     header->size);
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
						"binary requires unknown extension '%s'",
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
							"of the '%s' extension (compiled v%d, expected v%d;"
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

static bool
_sieve_binary_open(struct sieve_binary *sbin, enum sieve_error *error_code_r)
{
	bool result = TRUE;
	off_t offset = 0;
	struct sieve_binary_block *ext_block;
	unsigned int i;
	int ret;

	/* Read header */

	ret = sieve_binary_file_read_header(sbin, sbin->file->fd,
					    &sbin->header, error_code_r);
	if (ret < 0)
		return FALSE;
	offset = sbin->header.hdr_size;

	/* Load block index */

	for (i = 0; i < sbin->header.blocks && result; i++) {
		T_BEGIN {
			if (!_read_block_index_record(sbin, &offset, i))
				result = FALSE;
		} T_END;
	}

	if (!result) {
		*error_code_r = SIEVE_ERROR_NOT_VALID;
		return FALSE;
	}

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

	if (!result) {
		*error_code_r = SIEVE_ERROR_NOT_VALID;
		return FALSE;
	}
	return TRUE;
}

int sieve_binary_open(struct sieve_instance *svinst, const char *path,
		      struct sieve_script *script, struct sieve_binary **sbin_r,
		      enum sieve_error *error_code_r)
{
	struct sieve_binary_extension_reg *const *regs;
	unsigned int ext_count, i;
	struct sieve_binary *sbin;
	struct sieve_binary_file *file;

	i_assert(script == NULL || sieve_script_svinst(script) == svinst);
	*sbin_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);

	/* Create binary object */
	sbin = sieve_binary_create(svinst, script);
	sbin->path = p_strdup(sbin->pool, path);

	if (sieve_binary_file_open(sbin, path, &file, error_code_r) < 0) {
		sieve_binary_unref(&sbin);
		return -1;
	}

	sbin->file = file;

	event_set_append_log_prefix(
		sbin->event,
		t_strdup_printf("binary %s: ", path));

	if (!_sieve_binary_open(sbin, error_code_r)) {
		sieve_binary_unref(&sbin);
		return -1;
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
			*error_code_r = SIEVE_ERROR_NOT_VALID;
			sieve_binary_unref(&sbin);
			return -1;
		}
	}

	*sbin_r = sbin;
	return 0;
}

int sieve_binary_check_executable(struct sieve_binary *sbin,
				  enum sieve_error *error_code_r,
				  const char **client_error_r)
{
	*client_error_r = NULL;
	sieve_error_args_init(&error_code_r, NULL);

	if (HAS_ALL_BITS(sbin->header.flags,
			 SIEVE_BINARY_FLAG_RESOURCE_LIMIT)) {
		e_debug(sbin->event,
			"Binary execution is blocked: "
			"Cumulative resource usage limit exceeded "
			"(resource limit flag is set)");
		*error_code_r = SIEVE_ERROR_RESOURCE_LIMIT;
		*client_error_r = "cumulative resource usage limit exceeded";
		return 0;
	}
	return 1;
}

/*
 * Resource usage
 */

static int
sieve_binary_file_do_update_resource_usage(
	struct sieve_binary *sbin, int fd, enum sieve_error *error_code_r)
{
	struct sieve_binary_header *header = &sbin->header;
	struct file_lock *lock;
	const char *error;
	int ret;

	struct file_lock_settings lock_set = {
		.lock_method = FILE_LOCK_METHOD_FCNTL,
	};
	ret = file_wait_lock(fd, sbin->path, F_WRLCK, &lock_set,
			     SIEVE_BINARY_FILE_LOCK_TIMEOUT, &lock, &error);
	if (ret <= 0) {
		e_error(sbin->event, "%s", error);
		*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
		return -1;
	}

	ret = sieve_binary_file_read_header(sbin, fd, header, error_code_r);
	if (ret == 0) {
		sieve_binary_file_update_header(sbin);
		ret = sieve_binary_file_write_header(sbin, fd, header,
						     error_code_r);
	}

	file_lock_free(&lock);

	return ret;
}

int sieve_binary_file_update_resource_usage(struct sieve_binary *sbin,
					    enum sieve_error *error_code_r)
{
	int fd, ret = 0;

	sieve_error_args_init(&error_code_r, NULL);

	sieve_binary_file_close(&sbin->file);

	if (sbin->path == NULL)
		return 0;
	if (sbin->header.version_major != SIEVE_BINARY_VERSION_MAJOR) {
		return sieve_binary_save(sbin, sbin->path, TRUE, 0600,
					 error_code_r);
	}

	fd = sieve_binary_fd_open(sbin, sbin->path, O_RDWR, error_code_r);
	if (fd < 0) {
		i_assert(*error_code_r != SIEVE_ERROR_NONE);
		return -1;
	}

	ret = sieve_binary_file_do_update_resource_usage(sbin, fd,
							 error_code_r);
	i_assert(ret == 0 || *error_code_r != SIEVE_ERROR_NONE);

	if (close(fd) < 0) {
		e_error(sbin->event, "update: "
			"failed to close: close() failed: %m");
	}

	return ret;
}
