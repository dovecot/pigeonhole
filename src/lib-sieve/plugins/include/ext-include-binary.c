/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-script.h"
#include "sieve-binary.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "sieve-ext-variables.h"

#include "ext-include-common.h"
#include "ext-include-limits.h"
#include "ext-include-variables.h"
#include "ext-include-binary.h"

/*
 * Forward declarations
 */

static bool ext_include_binary_save
	(const struct sieve_extension *ext, struct sieve_binary *sbin, void *context);
static bool ext_include_binary_open
	(const struct sieve_extension *ext, struct sieve_binary *sbin, void *context);
static bool ext_include_binary_up_to_date
	(const struct sieve_extension *ext, struct sieve_binary *sbin, void *context,
		enum sieve_compile_flags cpflags);
static void ext_include_binary_free
	(const struct sieve_extension *ext, struct sieve_binary *sbin, void *context);

/*
 * Binary include extension
 */

const struct sieve_binary_extension include_binary_ext = {
	&include_extension,
	ext_include_binary_save,
	ext_include_binary_open,
	ext_include_binary_free,
	ext_include_binary_up_to_date
};

/*
 * Binary context management
 */

struct ext_include_binary_context {
	struct sieve_binary *binary;
	struct sieve_binary_block *dependency_block;

	HASH_TABLE(struct sieve_script *,
		   struct ext_include_script_info *) included_scripts;
	ARRAY(struct ext_include_script_info *) include_index;

	struct sieve_variable_scope_binary *global_vars;

	unsigned int outdated:1;
};

static struct ext_include_binary_context *ext_include_binary_create_context
(const struct sieve_extension *this_ext, struct sieve_binary *sbin)
{
	pool_t pool = sieve_binary_pool(sbin);

	struct ext_include_binary_context *ctx =
		p_new(pool, struct ext_include_binary_context, 1);

	ctx->binary = sbin;
	hash_table_create(&ctx->included_scripts, pool, 0,
		sieve_script_hash, sieve_script_cmp);
	p_array_init(&ctx->include_index, pool, 128);

	sieve_binary_extension_set(sbin, this_ext, &include_binary_ext, ctx);

	return ctx;
}

struct ext_include_binary_context *ext_include_binary_get_context
(const struct sieve_extension *this_ext, struct sieve_binary *sbin)
{
	struct ext_include_binary_context *ctx = (struct ext_include_binary_context *)
		sieve_binary_extension_get_context(sbin, this_ext);

	if ( ctx == NULL )
		ctx = ext_include_binary_create_context(this_ext, sbin);

	return ctx;
}

struct ext_include_binary_context *ext_include_binary_init
(const struct sieve_extension *this_ext, struct sieve_binary *sbin,
	struct sieve_ast *ast)
{
	struct ext_include_ast_context *ast_ctx =
		ext_include_get_ast_context(this_ext, ast);
	struct ext_include_binary_context *ctx;

	/* Get/create our context from the binary we are working on */
	ctx = ext_include_binary_get_context(this_ext, sbin);

	/* Create dependency block */
	if ( ctx->dependency_block == 0 )
		ctx->dependency_block =
			sieve_binary_extension_create_block(sbin, this_ext);

	if ( ctx->global_vars == NULL ) {
		ctx->global_vars =
			sieve_variable_scope_binary_create(ast_ctx->global_vars);
		sieve_variable_scope_binary_ref(ctx->global_vars);
	}

	return ctx;
}

/*
 * Script inclusion
 */

const struct ext_include_script_info *ext_include_binary_script_include
(struct ext_include_binary_context *binctx, 
	enum ext_include_script_location location, enum ext_include_flags flags,
	struct sieve_script *script,	struct sieve_binary_block *inc_block)
{
	pool_t pool = sieve_binary_pool(binctx->binary);
	struct ext_include_script_info *incscript;

	incscript = p_new(pool, struct ext_include_script_info, 1);
	incscript->id = array_count(&binctx->include_index)+1;
	incscript->location = location;
	incscript->flags = flags;
	incscript->script = script;
	incscript->block = inc_block;

	/* Unreferenced on binary_free */
	sieve_script_ref(script);

	hash_table_insert(binctx->included_scripts, script, incscript);
	array_append(&binctx->include_index, &incscript, 1);

	return incscript;
}

bool ext_include_binary_script_is_included
(struct ext_include_binary_context *binctx, struct sieve_script *script,
	const struct ext_include_script_info **script_info_r)
{
	struct ext_include_script_info *incscript =
		hash_table_lookup(binctx->included_scripts, script);

	if ( incscript == NULL )
		return FALSE;

	*script_info_r = incscript;
	return TRUE;
}

const struct ext_include_script_info *ext_include_binary_script_get_included
(struct ext_include_binary_context *binctx, unsigned int include_id)
{
	if ( include_id > 0 &&
		(include_id - 1) < array_count(&binctx->include_index) ) {
		struct ext_include_script_info *const *sinfo =
			array_idx(&binctx->include_index, include_id - 1);

		return *sinfo;
	}

	return NULL;
}

const struct ext_include_script_info *ext_include_binary_script_get
(struct ext_include_binary_context *binctx, struct sieve_script *script)
{
	return hash_table_lookup(binctx->included_scripts, script);
}

unsigned int ext_include_binary_script_get_count
(struct ext_include_binary_context *binctx)
{
	return array_count(&binctx->include_index);
}

/*
 * Variables
 */

struct sieve_variable_scope_binary *ext_include_binary_get_global_scope
(const struct sieve_extension *this_ext, struct sieve_binary *sbin)
{
	struct ext_include_binary_context *binctx =
		ext_include_binary_get_context(this_ext, sbin);

	return binctx->global_vars;
}

/*
 * Binary extension
 */

static bool ext_include_binary_save
(const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_binary *sbin ATTR_UNUSED, void *context)
{
	struct ext_include_binary_context *binctx =
		(struct ext_include_binary_context *) context;
	struct ext_include_script_info *const *scripts;
	struct sieve_binary_block *sblock = binctx->dependency_block;
	unsigned int script_count, i;
	bool result = TRUE;

	sieve_binary_block_clear(sblock);

	scripts = array_get(&binctx->include_index, &script_count);

	sieve_binary_emit_unsigned(sblock, script_count);

	for ( i = 0; i < script_count; i++ ) {
		struct ext_include_script_info *incscript = scripts[i];

		if ( incscript->block != NULL ) {
			sieve_binary_emit_unsigned
				(sblock, sieve_binary_block_get_id(incscript->block));
		} else {
			sieve_binary_emit_unsigned(sblock, 0);
		}
		sieve_binary_emit_byte(sblock, incscript->location);
		sieve_binary_emit_cstring(sblock, sieve_script_name(incscript->script));
		sieve_binary_emit_byte(sblock, incscript->flags);
		sieve_script_binary_write_metadata(incscript->script, sblock);
	}

	result = ext_include_variables_save(sblock, binctx->global_vars);

	return result;
}

static bool ext_include_binary_open
(const struct sieve_extension *ext, struct sieve_binary *sbin, void *context)
{
	struct sieve_instance *svinst = ext->svinst;
	struct ext_include_context *ext_ctx =
		(struct ext_include_context *)ext->context;
	struct ext_include_binary_context *binctx =
		(struct ext_include_binary_context *) context;
	struct sieve_binary_block *sblock;
	unsigned int depcount, i, block_id;
	sieve_size_t offset;

	sblock = sieve_binary_extension_get_block(sbin, ext);
	block_id = sieve_binary_block_get_id(sblock);

	offset = 0;

	if ( !sieve_binary_read_unsigned(sblock, &offset, &depcount) ) {
		sieve_sys_error(svinst,
			"include: failed to read include count "
			"for dependency block %d of binary %s", block_id,
			sieve_binary_path(sbin));
		return FALSE;
	}

	/* Check include limit */
	if ( depcount > ext_ctx->max_includes ) {
		sieve_sys_error(svinst,
			"include: binary %s includes too many scripts (%u > %u)",
			sieve_binary_path(sbin), depcount, ext_ctx->max_includes);
		return FALSE;
	}

	/* Read dependencies */
	for ( i = 0; i < depcount; i++ ) {
		unsigned int inc_block_id;
		struct sieve_binary_block *inc_block = NULL;
		unsigned int location, flags;
		string_t *script_name;
		const char *script_location;
		struct sieve_script *script;
		enum sieve_error error;
		int ret;

		if (
			!sieve_binary_read_unsigned(sblock, &offset, &inc_block_id) ||
			!sieve_binary_read_byte(sblock, &offset, &location) ||
			!sieve_binary_read_string(sblock, &offset, &script_name) ||
			!sieve_binary_read_byte(sblock, &offset, &flags) ) {
			/* Binary is corrupt, recompile */
			sieve_sys_error(svinst,
				"include: failed to read included script "
				"from dependency block %d of binary %s", block_id,
				sieve_binary_path(sbin));
			return FALSE;
		}

		if ( inc_block_id != 0 &&
			(inc_block=sieve_binary_block_get(sbin, inc_block_id)) == NULL ) {
			sieve_sys_error(svinst,
				"include: failed to find block %d for included script "
				"from dependency block %d of binary %s", inc_block_id, block_id,
				sieve_binary_path(sbin));
			return FALSE;
		}

		if ( location >= EXT_INCLUDE_LOCATION_INVALID ) {
			/* Binary is corrupt, recompile */
			sieve_sys_error(svinst,
				"include: dependency block %d of binary %s "
				"uses invalid script location (id %d)",
				block_id, sieve_binary_path(sbin), location);
			return FALSE;
		}

		/* Can we find the script dependency ? */
		script_location = ext_include_get_script_location
			(ext, location, str_c(script_name));
		if ( script_location == NULL ) {
			/* No, recompile */
			return FALSE;
		}

		/* Can we open the script dependency ? */
		script = sieve_script_create
			(ext->svinst, script_location, str_c(script_name), NULL, &error);
		if ( script == NULL ) {
			/* No, recompile */
			return FALSE;
		}
		if ( sieve_script_open(script, &error) < 0 ) {			
			if ( error != SIEVE_ERROR_NOT_FOUND ) {
				/* No, recompile */
				sieve_script_unref(&script);
				return FALSE;
			}

			if ( (flags & EXT_INCLUDE_FLAG_OPTIONAL) == 0 ) {
				/* Not supposed to be missing, recompile */
				if ( svinst->debug ) {
					sieve_sys_debug(svinst,
						"include: script '%s' included in binary %s is missing, "
						"so recompile", str_c(script_name), sieve_binary_path(sbin));
				}
				sieve_script_unref(&script);
				return FALSE;
			}

		} else if (inc_block == NULL) {
			/* Script exists, but it is missing from the binary, recompile no matter
			 * what.
			 */
			if ( svinst->debug ) {
				sieve_sys_debug(svinst,
					"include: script '%s' is missing in binary %s, but is now available, "
					"so recompile", str_c(script_name), sieve_binary_path(sbin));
			}
			sieve_script_unref(&script);
			return FALSE;
		}

		/* Can we read script metadata ? */
		if ( (ret=sieve_script_binary_read_metadata
			(script, sblock, &offset))	< 0 ) {
			/* Binary is corrupt, recompile */
			sieve_sys_error(svinst,
				"include: dependency block %d of binary %s "
				"contains invalid script metadata for script %s",
				block_id, sieve_binary_path(sbin), sieve_script_location(script));
			sieve_script_unref(&script);
			return FALSE;
		}

		if ( ret == 0 )
			binctx->outdated = TRUE;

		(void)ext_include_binary_script_include
			(binctx, location, flags, script, inc_block);

		sieve_script_unref(&script);
	}

	if ( !ext_include_variables_load
		(ext, sblock, &offset, &binctx->global_vars) )
		return FALSE;

	return TRUE;
}

static bool ext_include_binary_up_to_date
(const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_binary *sbin ATTR_UNUSED, void *context,
	enum sieve_compile_flags cpflags ATTR_UNUSED)
{
	struct ext_include_binary_context *binctx =
		(struct ext_include_binary_context *) context;

	return !binctx->outdated;
}

static void ext_include_binary_free
(const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_binary *sbin ATTR_UNUSED, void *context)
{
	struct ext_include_binary_context *binctx =
		(struct ext_include_binary_context *) context;
	struct hash_iterate_context *hctx;
	struct sieve_script *script;
	struct ext_include_script_info *incscript;

	/* Release references to all included script objects */
	hctx = hash_table_iterate_init(binctx->included_scripts);
	while ( hash_table_iterate
		(hctx, binctx->included_scripts, &script, &incscript) )
		sieve_script_unref(&incscript->script);
	hash_table_iterate_deinit(&hctx);

	hash_table_destroy(&binctx->included_scripts);

	if ( binctx->global_vars != NULL )
		sieve_variable_scope_binary_unref(&binctx->global_vars);
}

/*
 * Dumping the binary
 */

bool ext_include_binary_dump
(const struct sieve_extension *ext, struct sieve_dumptime_env *denv)
{
	struct sieve_binary *sbin = denv->sbin;
	struct ext_include_binary_context *binctx =
		ext_include_binary_get_context(ext, sbin);
	struct hash_iterate_context *hctx;
	struct sieve_script *script;
	struct ext_include_script_info *incscript;

	if ( !ext_include_variables_dump(denv, binctx->global_vars) )
		return FALSE;

	hctx = hash_table_iterate_init(binctx->included_scripts);
	while ( hash_table_iterate
		(hctx, binctx->included_scripts, &script, &incscript) ) {

		if ( incscript->block == NULL ) {
			sieve_binary_dump_sectionf(denv, "Included %s script '%s' (MISSING)",
				ext_include_script_location_name(incscript->location),
				sieve_script_name(incscript->script));

		} else {
			unsigned int block_id = sieve_binary_block_get_id(incscript->block);

			sieve_binary_dump_sectionf(denv, "Included %s script '%s' (block: %d)",
				ext_include_script_location_name(incscript->location),
				sieve_script_name(incscript->script), block_id);

			denv->sblock = incscript->block;
			denv->cdumper = sieve_code_dumper_create(denv);

			if ( denv->cdumper == NULL )
				return FALSE;

			sieve_code_dumper_run(denv->cdumper);
			sieve_code_dumper_free(&(denv->cdumper));
		}
	}
	hash_table_iterate_deinit(&hctx);

	return TRUE;
}

bool ext_include_code_dump
(const struct sieve_extension *ext, const struct sieve_dumptime_env *denv,
	sieve_size_t *address ATTR_UNUSED)
{
	struct sieve_binary *sbin = denv->sbin;
	struct ext_include_binary_context *binctx =
		ext_include_binary_get_context(ext, sbin);
	struct ext_include_context *ectx = ext_include_get_context(ext);

	sieve_ext_variables_dump_set_scope
		(ectx->var_ext, denv, ext,
			sieve_variable_scope_binary_get(binctx->global_vars));

	return TRUE;
}


