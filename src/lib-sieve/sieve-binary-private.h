#ifndef SIEVE_BINARY_PRIVATE_H
#define SIEVE_BINARY_PRIVATE_H

#include "sieve-common.h"
#include "sieve-binary.h"
#include "sieve-extensions.h"

#include <sys/stat.h>

#define SIEVE_BINARY_FILE_LOCK_TIMEOUT 10

/*
 * Binary file
 */

enum SIEVE_BINARY_FLAGS {
	SIEVE_BINARY_FLAG_RESOURCE_LIMIT = BIT(0),
};

struct sieve_binary_header {
	uint32_t magic;
	uint16_t version_major;
	uint16_t version_minor;
	uint32_t blocks;

	uint32_t hdr_size;
	uint32_t flags;

	struct {
		uint64_t update_time;
		uint32_t cpu_time_msecs;
	} resource_usage;
};

struct sieve_binary_file {
	pool_t pool;
	const char *path;
	struct sieve_binary *sbin;

	struct stat st;
	int fd;
	off_t offset;
};

void sieve_binary_file_close(struct sieve_binary_file **_file);

/*
 * Internal structures
 */

/* Extension registration */

struct sieve_binary_extension_reg {
	/* The identifier of the extension within this binary */
	int index;

	/* Global extension object */
	const struct sieve_extension *extension;

	/* Extension to the binary; typically used to manage extension-specific
	   blocks in the binary and as a means to get a binary_free notification
	   to release references held by extensions.
	 */
	const struct sieve_binary_extension *binext;

	/* Context data associated to the binary by this extension */
	void *context;

	/* Main block for this extension */
	unsigned int block_id;
};

/* Block */

struct sieve_binary_block {
	struct sieve_binary *sbin;
	unsigned int id;
	int ext_index;

	buffer_t *data;

	uoff_t offset;
};

/*
 * Binary object
 */

struct sieve_binary {
	pool_t pool;
	int refcount;
	struct sieve_instance *svinst;
	struct event *event;

	struct sieve_script *script;

	struct sieve_binary_file *file;
	struct sieve_binary_header header;
	struct sieve_resource_usage rusage;

	/* When the binary is loaded into memory or when it is being constructed
	   by the generator, extensions can be associated to the binary. The
	   extensions array is a sequential list of all linked extensions. The
	   extension_index array is a mapping ext_id -> binary_extension. This
	   is used to obtain the index code associated with an extension for
	   this particular binary. The linked_extensions list all extensions
	   linked to this binary object other than the preloaded language
	   features implemented as 'extensions'.

	   All arrays refer to the same extension registration objects. Upon
	   loading a binary, the 'require'd extensions will sometimes need to
	   associate context data to the binary object in memory. This is stored
	   in these registration objects as well.
	 */
	ARRAY(struct sieve_binary_extension_reg *) extensions;
	ARRAY(struct sieve_binary_extension_reg *) extension_index;
	ARRAY(struct sieve_binary_extension_reg *) linked_extensions;

	/* Attributes of a loaded binary */
	const char *path;

	/* Blocks */
	ARRAY(struct sieve_binary_block *) blocks;

	bool rusage_updated:1;
};

void sieve_binary_update_event(struct sieve_binary *sbin, const char *new_path)
			       ATTR_NULL(2);

struct sieve_binary *
sieve_binary_create(struct sieve_instance *svinst, struct sieve_script *script);

/* Blocks management */

static inline struct sieve_binary_block *
sieve_binary_block_index(struct sieve_binary *sbin, unsigned int id)
{
	struct sieve_binary_block * const *sblock;

	if (id >= array_count(&sbin->blocks))
		return NULL;

	sblock = array_idx(&sbin->blocks, id);
	if (*sblock == NULL)
		return NULL;
	return *sblock;
}

static inline size_t
_sieve_binary_block_get_size(const struct sieve_binary_block *sblock)
{
	return buffer_get_used_size(sblock->data);
}

struct sieve_binary_block *
sieve_binary_block_create_id(struct sieve_binary *sbin, unsigned int id);

buffer_t *sieve_binary_block_get_buffer(struct sieve_binary_block *sblock);

/* Extension registration */

static inline struct sieve_binary_extension_reg *
sieve_binary_extension_create_reg(struct sieve_binary *sbin,
				  const struct sieve_extension *ext)
{
	int index = array_count(&sbin->extensions);
	struct sieve_binary_extension_reg *ereg;

	if (ext->id < 0)
		return NULL;

	ereg = p_new(sbin->pool, struct sieve_binary_extension_reg, 1);
	ereg->index = index;
	ereg->extension = ext;

	array_idx_set(&sbin->extensions, (unsigned int) index, &ereg);
	array_idx_set(&sbin->extension_index, (unsigned int) ext->id, &ereg);

	return ereg;
}

static inline struct sieve_binary_extension_reg *
sieve_binary_extension_get_reg(struct sieve_binary *sbin,
			       const struct sieve_extension *ext,
			       bool create)
{
	struct sieve_binary_extension_reg *reg = NULL;

	if (ext->id >= 0 &&
	    ext->id < (int)array_count(&sbin->extension_index)) {
		struct sieve_binary_extension_reg * const *ereg =
			array_idx(&sbin->extension_index,
				  (unsigned int)ext->id);

		reg = *ereg;
	}

	/* Register if not known */
	if (reg == NULL && create)
		return sieve_binary_extension_create_reg(sbin, ext);
	return reg;
}

static inline int
sieve_binary_extension_register(struct sieve_binary *sbin,
				const struct sieve_extension *ext,
				struct sieve_binary_extension_reg **reg_r)
{
	struct sieve_binary_extension_reg *ereg;

	if ((ereg = sieve_binary_extension_get_reg(sbin, ext, FALSE)) == NULL) {
		ereg = sieve_binary_extension_create_reg(sbin, ext);

		if (ereg == NULL)
			return -1;

		array_append(&sbin->linked_extensions, &ereg, 1);
	}

	if (reg_r != NULL)
		*reg_r = ereg;
	return ereg->index;
}

/* Load/Save */

bool sieve_binary_load_block(struct sieve_binary_block *);

/*
 * Resource limits
 */

bool sieve_binary_check_resource_usage(struct sieve_binary *sbin);

int sieve_binary_file_update_resource_usage(struct sieve_binary *sbin,
					    enum sieve_error *error_r);

#endif
