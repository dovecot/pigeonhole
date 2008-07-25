#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-script.h"
#include "sieve-binary.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-include-common.h"
#include "ext-include-binary.h"

/*
 * Types
 */
 
struct _included_script {
	struct sieve_script *script;
	enum ext_include_script_location location;
	
	unsigned int block_id;
};
 
struct ext_include_binary_context {
	struct sieve_binary *binary;
	unsigned int dependency_block;
	
	struct hash_table *included_scripts;
};

/*
 * Forward declarations
 */
 
static bool ext_include_binary_save(struct sieve_binary *sbin);
static bool ext_include_binary_open(struct sieve_binary *sbin);
static bool ext_include_binary_up_to_date(struct sieve_binary *sbin);
static void ext_include_binary_free(struct sieve_binary *sbin);

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
 
static struct ext_include_binary_context *ext_include_binary_create_context
(struct sieve_binary *sbin)
{
	pool_t pool = sieve_binary_pool(sbin);
	
	struct ext_include_binary_context *ctx = 
		p_new(pool, struct ext_include_binary_context, 1);
	
	ctx->binary = sbin;			
	ctx->included_scripts = hash_create(default_pool, pool, 0, 
		(hash_callback_t *) sieve_script_hash, 
		(hash_cmp_callback_t *) sieve_script_cmp);
	
	return ctx;
}

static inline struct ext_include_binary_context *ext_include_binary_get_context
(struct sieve_binary *sbin)
{	
	struct ext_include_binary_context *ctx = (struct ext_include_binary_context *)
		sieve_binary_extension_get_context(sbin, &include_extension);
	
	if ( ctx == NULL ) {
		ctx = ext_include_binary_create_context(sbin);
		sieve_binary_extension_set_context(sbin, &include_extension, ctx);
	};
	
	return ctx;
}

/* 
 * Binary include implementation 
 */
 
struct ext_include_binary_context *ext_include_binary_init
(struct sieve_binary *sbin)
{
	struct ext_include_binary_context *ctx;
	
	/* Get/create our context from the binary we are working on */
	ctx = ext_include_binary_get_context(sbin);
	
	/* Create dependency block */
	if ( ctx->dependency_block == 0 )
		ctx->dependency_block = 
			sieve_binary_extension_create_block(sbin, &include_extension);
			
	return ctx;
}

void ext_include_binary_script_include
(struct ext_include_binary_context *binctx, struct sieve_script *script,
	enum ext_include_script_location location, unsigned int block_id)
{
	pool_t pool = sieve_binary_pool(binctx->binary);
	struct _included_script *incscript;
	
	incscript = p_new(pool, struct _included_script, 1);
	incscript->script = script;
	incscript->location = location;
	incscript->block_id = block_id;
	
	/* Unreferenced on binary_free */
	sieve_script_ref(script);
	
	hash_insert(binctx->included_scripts, (void *) script, (void *) incscript);
}

bool ext_include_binary_script_is_included
(struct ext_include_binary_context *binctx, struct sieve_script *script,
	unsigned int *block_id)
{
	struct _included_script *incscript = (struct _included_script *)
		hash_lookup(binctx->included_scripts, script);
		
	if ( incscript == 0 )
		return FALSE;
				
	*block_id = incscript->block_id;
	return TRUE;
}

static bool ext_include_binary_save(struct sieve_binary *sbin)
{
	struct ext_include_binary_context *binctx = 
		ext_include_binary_get_context(sbin);
	struct hash_iterate_context *hctx = 
		hash_iterate_init(binctx->included_scripts);
	void *key, *value;
	unsigned int prvblk;
	
	sieve_binary_block_clear(sbin, binctx->dependency_block);
	if ( !sieve_binary_block_set_active(sbin, binctx->dependency_block, &prvblk) )	
		return FALSE;
			
	sieve_binary_emit_integer(sbin, hash_count(binctx->included_scripts));	
	while ( hash_iterate(hctx, &key, &value) ) {
		struct _included_script *incscript = (struct _included_script *) value;

		sieve_binary_emit_integer(sbin, incscript->block_id);
		sieve_binary_emit_byte(sbin, incscript->location);
		sieve_binary_emit_cstring(sbin, sieve_script_name(incscript->script));
	}
	
	(void) sieve_binary_block_set_active(sbin, prvblk, NULL);

	hash_iterate_deinit(&hctx);
	
	return TRUE;
}

static bool ext_include_binary_open(struct sieve_binary *sbin)
{
	struct ext_include_binary_context *binctx; 
	unsigned int block, prvblk, depcount, i;
	sieve_size_t offset;
	
	block = sieve_binary_extension_get_block(sbin, &include_extension);
	
	if ( !sieve_binary_block_set_active(sbin, block, &prvblk) )
		return FALSE; 
		
	offset = 0;	
		
	if ( !sieve_binary_read_integer(sbin, &offset, &depcount) ) {
		sieve_sys_error("include: failed to read include count "
			"for dependency block %d of binary %s", block, sieve_binary_path(sbin)); 
		return FALSE;
	}
	
	binctx = ext_include_binary_get_context(sbin);
		
	/* Read dependencies */
	for ( i = 0; i < depcount; i++ ) {
		unsigned int block_id;
		enum ext_include_script_location location;
		string_t *script_name;
		const char *script_dir;
		struct sieve_script *script;
		
		if ( 
			!sieve_binary_read_integer(sbin, &offset, &block_id) ||
			!sieve_binary_read_byte(sbin, &offset, &location) ||
			!sieve_binary_read_string(sbin, &offset, &script_name) ) {
			/* Binary is corrupt, recompile */
			sieve_sys_error("include: failed to read included script "
				"from dependency block %d of binary %s", block, sieve_binary_path(sbin)); 
			return FALSE;
		}
		
		if ( location >= EXT_INCLUDE_LOCATION_INVALID ) {
			/* Binary is corrupt, recompile */
			sieve_sys_error("include: dependency block %d of binary %s "
				"reports invalid script location (id %d).", 
				block, sieve_binary_path(sbin), location); 
			return FALSE;
		}		
		
		/* Can we find/open the script dependency ? */
		script_dir = ext_include_get_script_directory(location, str_c(script_name));		
		if ( script_dir == NULL || 
			!(script=sieve_script_create_in_directory
				(script_dir, str_c(script_name), NULL, NULL)) ) {
			/* No, recompile */
			return FALSE;
		}
		
		ext_include_binary_script_include(binctx, script, location, block_id);
				
		sieve_script_unref(&script);
	}
	
	/* Restore previously active block */
	(void)sieve_binary_block_set_active(sbin, prvblk, NULL);

	return TRUE;	
}

static bool ext_include_binary_up_to_date(struct sieve_binary *sbin)
{
	struct ext_include_binary_context *binctx = 
		ext_include_binary_get_context(sbin);
	struct hash_iterate_context *hctx;
	void *key, *value;
		
	/* Release references to all included script objects */
	hctx = hash_iterate_init(binctx->included_scripts);
	while ( hash_iterate(hctx, &key, &value) ) {
		struct _included_script *incscript = (struct _included_script *) value;
		
		/* Is the binary newer than this dependency? */
		if ( !sieve_binary_script_older(sbin, incscript->script) ) {
			/* No, recompile */
			return FALSE;
		}
	}
	hash_iterate_deinit(&hctx);

	return TRUE;
}

static void ext_include_binary_free(struct sieve_binary *sbin)
{
	struct ext_include_binary_context *binctx = 
		ext_include_binary_get_context(sbin);
	struct hash_iterate_context *hctx;
	void *key, *value;
		
	/* Release references to all included script objects */
	hctx = hash_iterate_init(binctx->included_scripts);
	while ( hash_iterate(hctx, &key, &value) ) {
		struct _included_script *incscript = (struct _included_script *) value;
		
		sieve_script_unref(&incscript->script);
	}
	hash_iterate_deinit(&hctx);

	hash_destroy(&binctx->included_scripts);
}

inline static const char *_script_location
(enum ext_include_script_location loc)
{
	switch ( loc ) {
	case EXT_INCLUDE_LOCATION_PERSONAL:
		return "personal";
	case EXT_INCLUDE_LOCATION_GLOBAL:
		return "global";
	default:
		break;
	}
	
	return "<<INVALID LOCATION>>";
}

bool ext_include_binary_dump(struct sieve_dumptime_env *denv)
{
	struct sieve_binary *sbin = denv->sbin;
	struct ext_include_binary_context *binctx = 
		ext_include_binary_get_context(sbin);
	struct hash_iterate_context *hctx = 
		hash_iterate_init(binctx->included_scripts);
	void *key, *value;
	unsigned int prvblk = 0;
				
	while ( hash_iterate(hctx, &key, &value) ) {
		struct _included_script *incscript = (struct _included_script *) value;

		sieve_binary_dump_sectionf(denv, "Included %s script '%s' (block: %d)", 
			_script_location(incscript->location), 
			sieve_script_name(incscript->script), incscript->block_id);
			
		if ( prvblk == 0 ) {
			if ( !sieve_binary_block_set_active(sbin, incscript->block_id, &prvblk) )	
				return FALSE;
		} else {
			if ( !sieve_binary_block_set_active(sbin, incscript->block_id, NULL) )	
				return FALSE;
		}
				
		denv->cdumper = sieve_code_dumper_create(denv);
		sieve_code_dumper_run(denv->cdumper);
		sieve_code_dumper_free(&(denv->cdumper));
	}
	
	if ( !sieve_binary_block_set_active(sbin, prvblk, NULL) ) 
		return FALSE;
	
	hash_iterate_deinit(&hctx);
	
	return TRUE;	
}

