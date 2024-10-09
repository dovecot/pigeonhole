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

#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-script.h"

#include "sieve-binary-private.h"

/*
 * Forward declarations
 */

static inline struct sieve_binary_extension_reg *
sieve_binary_extension_get_reg(struct sieve_binary *sbin,
			       const struct sieve_extension *ext, bool create);

static inline int
sieve_binary_extension_register(struct sieve_binary *sbin,
				const struct sieve_extension *ext,
				struct sieve_binary_extension_reg **reg);

/*
 * Binary object
 */

void sieve_binary_update_event(struct sieve_binary *sbin, const char *new_path)
{
	if (new_path != NULL) {
		event_set_append_log_prefix(
			sbin->event, t_strdup_printf("binary %s: ", new_path));
	} else if (sbin->path != NULL) {
		event_set_append_log_prefix(
			sbin->event,
			t_strdup_printf("binary %s: ", sbin->path));
	} else if (sbin->script != NULL) {
		event_set_append_log_prefix(
			sbin->event,
			t_strdup_printf("binary %s: ",
					sieve_script_name(sbin->script)));
	} else {
		event_set_append_log_prefix(sbin->event, "binary: ");
	}
}

struct sieve_binary *
sieve_binary_create(struct sieve_instance *svinst, struct sieve_script *script)
{
	pool_t pool;
	struct sieve_binary *sbin;
	const struct sieve_extension *const *ext_preloaded;
	unsigned int i, ext_count;

	pool = pool_alloconly_create("sieve_binary", 16384);
	sbin = p_new(pool, struct sieve_binary, 1);
	sbin->pool = pool;
	sbin->refcount = 1;
	sbin->svinst = svinst;

	sbin->header.version_major = SIEVE_BINARY_VERSION_MAJOR;
	sbin->header.version_minor = SIEVE_BINARY_VERSION_MINOR;

	sbin->script = script;
	if (script != NULL)
		sieve_script_ref(script);

	sbin->event = event_create(svinst->event);

	ext_count = sieve_extensions_get_count(svinst);

	p_array_init(&sbin->linked_extensions, pool, ext_count);
	p_array_init(&sbin->extensions, pool, ext_count);
	p_array_init(&sbin->extension_index, pool, ext_count);

	p_array_init(&sbin->blocks, pool, 16);

	/* Pre-load core language features implemented as 'extensions' */
	ext_preloaded = sieve_extensions_get_preloaded(svinst, &ext_count);
	for (i = 0; i < ext_count; i++) {
		const struct sieve_extension_def *ext_def = ext_preloaded[i]->def;

		if (ext_def != NULL && ext_def->binary_load != NULL)
			(void)ext_def->binary_load(ext_preloaded[i], sbin);
	}

	return sbin;
}

struct sieve_binary *sieve_binary_create_new(struct sieve_script *script)
{
	struct sieve_binary *sbin =
		sieve_binary_create(sieve_script_svinst(script), script);
	struct sieve_binary_block *sblock;
	unsigned int i;

	sieve_binary_update_event(sbin, NULL);

	/* Create script metadata block */
	sblock = sieve_binary_block_create(sbin);
	sieve_script_binary_write_metadata(script, sblock);

	/* Create other system blocks */
	for (i = 1; i < SBIN_SYSBLOCK_LAST; i++)
		(void)sieve_binary_block_create(sbin);

	return sbin;
}

void sieve_binary_ref(struct sieve_binary *sbin)
{
	sbin->refcount++;
}

static inline void sieve_binary_extensions_free(struct sieve_binary *sbin)
{
	struct sieve_binary_extension_reg *const *regs;
	unsigned int ext_count, i;

	/* Cleanup binary extensions */
	regs = array_get(&sbin->extensions, &ext_count);
	for (i = 0; i < ext_count; i++) {
		const struct sieve_binary_extension *binext = regs[i]->binext;

		if (binext != NULL && binext->binary_free != NULL) {
			binext->binary_free(regs[i]->extension, sbin,
					    regs[i]->context);
		}
	}
}

static void sieve_binary_update_resource_usage(struct sieve_binary *sbin)
{
	enum sieve_error error_code;

	if (sbin->rusage_updated) {
		(void)sieve_binary_file_update_resource_usage(
			sbin, &error_code);
	}
	sbin->rusage_updated = FALSE;
}

void sieve_binary_unref(struct sieve_binary **_sbin)
{
	struct sieve_binary *sbin = *_sbin;

	*_sbin = NULL;
	if (sbin == NULL)
		return;

	i_assert(sbin->refcount > 0);
	if (--sbin->refcount != 0)
		return;

	sieve_binary_file_close(&sbin->file);
	sieve_binary_update_resource_usage(sbin);
	sieve_binary_extensions_free(sbin);

	sieve_script_unref(&sbin->script);

	event_unref(&sbin->event);
	pool_unref(&sbin->pool);
}

void sieve_binary_close(struct sieve_binary **_sbin)
{
	struct sieve_binary *sbin = *_sbin;

	*_sbin = NULL;
	if (sbin == NULL)
		return;

	sieve_binary_file_close(&sbin->file);
	sieve_binary_update_resource_usage(sbin);
	sieve_binary_unref(&sbin);
}

/*
 * Resource usage
 */

void sieve_binary_get_resource_usage(struct sieve_binary *sbin,
				     struct sieve_resource_usage *rusage_r)
{
	struct sieve_binary_header *header = &sbin->header;
	time_t update_time = header->resource_usage.update_time;
	unsigned int timeout = sbin->svinst->set->resource_usage_timeout;

	if (update_time != 0 && (ioloop_time - update_time) > (time_t)timeout)
		i_zero(&header->resource_usage);

	sieve_resource_usage_init(rusage_r);
	rusage_r->cpu_time_msecs = header->resource_usage.cpu_time_msecs;
	sieve_resource_usage_add(rusage_r, &sbin->rusage);
}

bool sieve_binary_check_resource_usage(struct sieve_binary *sbin)
{
	struct sieve_binary_header *header = &sbin->header;
	struct sieve_resource_usage rusage;

	sieve_binary_get_resource_usage(sbin, &rusage);

	if (sieve_resource_usage_is_excessive(sbin->svinst, &rusage)) {
		header->flags |= SIEVE_BINARY_FLAG_RESOURCE_LIMIT;
		return FALSE;
	}
	return TRUE;
}

bool sieve_binary_record_resource_usage(
	struct sieve_binary *sbin, const struct sieve_resource_usage *rusage)
{
	struct sieve_resource_usage rusage_total;

	if (sbin == NULL)
		return TRUE;
	if (!sieve_resource_usage_is_high(sbin->svinst, rusage))
		return TRUE;

	sieve_resource_usage_add(&sbin->rusage, rusage);
	sbin->rusage_updated = TRUE;

	sieve_binary_get_resource_usage(sbin, &rusage_total);

	e_debug(sbin->event, "Updated cumulative resource usage: %s",
		sieve_resource_usage_get_summary(&rusage_total));

	return sieve_binary_check_resource_usage(sbin);
}

void sieve_binary_set_resource_usage(struct sieve_binary *sbin,
				     const struct sieve_resource_usage *rusage)
{
	struct sieve_binary_header *header = &sbin->header;

	i_zero(&header->resource_usage);
	sbin->rusage = *rusage;
	sbin->rusage_updated = TRUE;

	(void)sieve_binary_check_resource_usage(sbin);
}

/*
 * Accessors
 */

pool_t sieve_binary_pool(struct sieve_binary *sbin)
{
	return sbin->pool;
}

struct sieve_script *sieve_binary_script(struct sieve_binary *sbin)
{
	return sbin->script;
}

const char *sieve_binary_path(struct sieve_binary *sbin)
{
	return sbin->path;
}

bool sieve_binary_saved(struct sieve_binary *sbin)
{
	return (sbin->path != NULL);
}

bool sieve_binary_loaded(struct sieve_binary *sbin)
{
	return (sbin->file != NULL);
}

const char *sieve_binary_source(struct sieve_binary *sbin)
{
	if (sbin->script != NULL && (sbin->path == NULL || sbin->file == NULL))
		return sieve_script_label(sbin->script);

	return sbin->path;
}

struct sieve_instance *sieve_binary_svinst(struct sieve_binary *sbin)
{
	return sbin->svinst;
}

time_t sieve_binary_mtime(struct sieve_binary *sbin)
{
	i_assert(sbin->file != NULL);
	return sbin->file->st.st_mtime;
}

const struct stat *sieve_binary_stat(struct sieve_binary *sbin)
{
	i_assert(sbin->file != NULL);
	return &sbin->file->st;
}

const char *sieve_binary_script_name(struct sieve_binary *sbin)
{
	return (sbin->script == NULL ?
		NULL : sieve_script_name(sbin->script));
}

const char *sieve_binary_script_location(struct sieve_binary *sbin)
{
	return (sbin->script == NULL ?
		NULL : sieve_script_label(sbin->script));
}

/*
 * Utility
 */

const char *sieve_binfile_from_name(const char *name)
{
	return t_strconcat(name, "."SIEVE_BINARY_FILEEXT, NULL);
}

/*
 * Block management
 */

unsigned int sieve_binary_block_count(struct sieve_binary *sbin)
{
	return array_count(&sbin->blocks);
}

struct sieve_binary_block *sieve_binary_block_create(struct sieve_binary *sbin)
{
	unsigned int id = sieve_binary_block_count(sbin);
	struct sieve_binary_block *sblock;

	sblock = p_new(sbin->pool, struct sieve_binary_block, 1);
	sblock->data = buffer_create_dynamic(sbin->pool, 64);
	sblock->sbin = sbin;
	sblock->id = id;

	array_append(&sbin->blocks, &sblock, 1);

	return sblock;
}

struct sieve_binary_block *
sieve_binary_block_create_id(struct sieve_binary *sbin, unsigned int id)
{
	struct sieve_binary_block *sblock;

	sblock = p_new(sbin->pool, struct sieve_binary_block, 1);

	array_idx_set(&sbin->blocks, id, &sblock);
	sblock->data = NULL;
	sblock->sbin = sbin;
	sblock->id = id;

	return sblock;
}

static bool sieve_binary_block_fetch(struct sieve_binary_block *sblock)
{
	struct sieve_binary *sbin = sblock->sbin;

	if (sbin->file != NULL) {
		/* Try to acces the block in the binary on disk (apparently we
		   were lazy)
		 */
		if (!sieve_binary_load_block(sblock) || sblock->data == NULL)
			return FALSE;
	} else {
		sblock->data = buffer_create_dynamic(sbin->pool, 64);
		return TRUE;
	}

	return TRUE;
}

struct sieve_binary_block *
sieve_binary_block_get(struct sieve_binary *sbin, unsigned int id)
{
	struct sieve_binary_block *sblock = sieve_binary_block_index(sbin, id);

	if (sblock == NULL)
		return NULL;

	if (sblock->data == NULL && !sieve_binary_block_fetch(sblock))
		return NULL;

	return sblock;
}

void sieve_binary_block_clear(struct sieve_binary_block *sblock)
{
	buffer_set_used_size(sblock->data, 0);
}

buffer_t *sieve_binary_block_get_buffer(struct sieve_binary_block *sblock)
{
	if (sblock->data == NULL && !sieve_binary_block_fetch(sblock))
		return NULL;

	return sblock->data;
}

struct sieve_binary *
sieve_binary_block_get_binary(const struct sieve_binary_block *sblock)
{
	return sblock->sbin;
}

unsigned int sieve_binary_block_get_id(const struct sieve_binary_block *sblock)
{
	return sblock->id;
}

size_t sieve_binary_block_get_size(const struct sieve_binary_block *sblock)
{
	return _sieve_binary_block_get_size(sblock);
}

/*
 * Up-to-date checking
 */

bool sieve_binary_up_to_date(struct sieve_binary *sbin,
			     enum sieve_compile_flags cpflags)
{
	struct sieve_binary_extension_reg *const *regs;
	struct sieve_binary_block *sblock;
	sieve_size_t offset = 0;
	unsigned int ext_count, i;
	int ret;

	i_assert(sbin->file != NULL);

	sblock = sieve_binary_block_get(sbin, SBIN_SYSBLOCK_SCRIPT_DATA);
	if (sblock == NULL || sbin->script == NULL)
		return FALSE;

	if ((ret = sieve_script_binary_read_metadata(sbin->script, sblock,
						     &offset)) <= 0) {
		if (ret < 0) {
			e_debug(sbin->event, "up-to-date: "
				"failed to read script metadata from binary");
		} else {
			e_debug(sbin->event, "up-to-date: "
				"script metadata indicates that binary is not up-to-date");
		}
		return FALSE;
	}

	regs = array_get(&sbin->extensions, &ext_count);
	for (i = 0; i < ext_count; i++) {
		const struct sieve_binary_extension *binext = regs[i]->binext;

		if (binext != NULL && binext->binary_up_to_date != NULL &&
		    !binext->binary_up_to_date(regs[i]->extension, sbin,
					       regs[i]->context, cpflags)) {
			e_debug(sbin->event, "up-to-date: "
				"the %s extension indicates binary is not up-to-date",
				sieve_extension_name(regs[i]->extension));
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * Activate the binary (after code generation)
 */

void sieve_binary_activate(struct sieve_binary *sbin)
{
	struct sieve_binary_extension_reg *const *regs;
	unsigned int i, ext_count;

	/* Load other extensions into binary */
	regs = array_get(&sbin->linked_extensions, &ext_count);
	for (i = 0; i < ext_count; i++) {
		const struct sieve_extension *ext = regs[i]->extension;

		if (ext != NULL && ext->def != NULL &&
		    ext->def->binary_load != NULL)
			ext->def->binary_load(ext, sbin);
	}
}

/*
 * Extension handling
 */

void sieve_binary_extension_set_context(struct sieve_binary *sbin,
					const struct sieve_extension *ext,
					void *context)
{
	struct sieve_binary_extension_reg *ereg =
		sieve_binary_extension_get_reg(sbin, ext, TRUE);

	if (ereg != NULL)
		ereg->context = context;
}

const void *
sieve_binary_extension_get_context(struct sieve_binary *sbin,
				   const struct sieve_extension *ext)
{
	struct sieve_binary_extension_reg *ereg =
		sieve_binary_extension_get_reg(sbin, ext, TRUE);

	if (ereg != NULL)
		return ereg->context;

	return NULL;
}

void sieve_binary_extension_set(struct sieve_binary *sbin,
				const struct sieve_extension *ext,
				const struct sieve_binary_extension *bext,
				void *context)
{
	struct sieve_binary_extension_reg *ereg =
		sieve_binary_extension_get_reg(sbin, ext, TRUE);

	if (ereg != NULL) {
		ereg->binext = bext;

		if (context != NULL)
			ereg->context = context;
	}
}

struct sieve_binary_block *
sieve_binary_extension_create_block(struct sieve_binary *sbin,
				    const struct sieve_extension *ext)
{
	struct sieve_binary_block *sblock;
	struct sieve_binary_extension_reg *ereg =
		sieve_binary_extension_get_reg(sbin, ext, TRUE);

	i_assert(ereg != NULL);

	sblock = sieve_binary_block_create(sbin);

	if (ereg->block_id < SBIN_SYSBLOCK_LAST)
		ereg->block_id = sblock->id;
	sblock->ext_index = ereg->index;

	return sblock;
}

struct sieve_binary_block *
sieve_binary_extension_get_block(struct sieve_binary *sbin,
				 const struct sieve_extension *ext)
{
	struct sieve_binary_extension_reg *ereg =
		sieve_binary_extension_get_reg(sbin, ext, TRUE);

	i_assert(ereg != NULL);

	if (ereg->block_id < SBIN_SYSBLOCK_LAST)
		return NULL;

	return sieve_binary_block_get(sbin, ereg->block_id);
}

int sieve_binary_extension_link(struct sieve_binary *sbin,
				const struct sieve_extension *ext)
{
	return sieve_binary_extension_register(sbin, ext, NULL);
}

const struct sieve_extension *
sieve_binary_extension_get_by_index(struct sieve_binary *sbin, int index)
{
	struct sieve_binary_extension_reg *const *ereg;

	if (index < (int)array_count(&sbin->extensions)) {
		ereg = array_idx(&sbin->extensions, (unsigned int)index);

		return (*ereg)->extension;
	}

	return NULL;
}

int sieve_binary_extension_get_index(struct sieve_binary *sbin,
				     const struct sieve_extension *ext)
{
	struct sieve_binary_extension_reg *ereg =
		sieve_binary_extension_get_reg(sbin, ext, FALSE);

	return (ereg == NULL ? -1 : ereg->index);
}

int sieve_binary_extensions_count(struct sieve_binary *sbin)
{
	return (int)array_count(&sbin->extensions);
}
