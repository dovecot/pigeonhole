#include <stdio.h>

#include "lib.h"
#include "compat.h"
#include "hash.h"
#include "array.h"

#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "sieve-comparators.h"

#include <string.h>

/* 
 * Predeclarations 
 */
 
extern const struct sieve_comparator *sieve_core_comparators[];
extern const unsigned int sieve_core_comparators_count;

static void opr_comparator_emit
	(struct sieve_binary *sbin, struct sieve_comparator *cmp);
static void opr_comparator_emit_ext
	(struct sieve_binary *sbin, struct sieve_comparator *cmp, int ext_id);

static int cmp_i_octet_compare
	(const void *val1, size_t val1_size, const void *val2, size_t val2_size);
static int cmp_i_ascii_casemap_compare
	(const void *val1, size_t val1_size, const void *val2, size_t val2_size);

/* 
 * Comparator 'extension' 
 */

static int ext_my_id = -1;

static bool cmp_extension_load(int ext_id);
static bool cmp_validator_load(struct sieve_validator *validator);
static bool cmp_interpreter_load(struct sieve_interpreter *interp);

const struct sieve_extension comparator_extension = {
	"@comparator",
	cmp_extension_load,
	cmp_validator_load,
	NULL,
	cmp_interpreter_load,
	NULL,
	NULL
};
	
static bool cmp_extension_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* 
 * Validator context:
 *   name-based address-part registry. 
 *
 * FIXME: This code will be duplicated across all extensions that introduce 
 * a registry of some kind in the validator. 
 */
 
struct cmp_validator_registration {
	int ext_id;
	const struct sieve_comparator *address_part;
};
 
struct cmp_validator_context {
	struct hash_table *registrations;
};

static inline struct cmp_validator_context *
	get_validator_context(struct sieve_validator *validator)
{
	return (struct cmp_validator_context *) 
		sieve_validator_extension_get_context(validator, ext_my_id);
}

static void _sieve_comparator_register
	(pool_t pool, struct cmp_validator_context *ctx, 
	const struct sieve_comparator *cmp, int ext_id) 
{
	struct cmp_validator_registration *reg;
	
	reg = p_new(pool, struct cmp_validator_registration, 1);
	reg->address_part = cmp;
	reg->ext_id = ext_id;
	
	hash_insert(ctx->registrations, (void *) cmp->identifier, (void *) reg);
}
 
void sieve_comparator_register
	(struct sieve_validator *validator, 
	const struct sieve_comparator *cmp, int ext_id) 
{
	pool_t pool = sieve_validator_pool(validator);
	struct cmp_validator_context *ctx = get_validator_context(validator);

	_sieve_comparator_register(pool, ctx, cmp, ext_id);
}

const struct sieve_comparator *sieve_comparator_find
	(struct sieve_validator *validator, const char *identifier,
		int *ext_id) 
{
	struct cmp_validator_context *ctx = get_validator_context(validator);
	struct cmp_validator_registration *reg =
		(struct cmp_validator_registration *) 
			hash_lookup(ctx->registrations, identifier);
			
	if ( reg == NULL ) return NULL;

	if ( ext_id != NULL ) *ext_id = reg->ext_id;

  return reg->address_part;
}

bool cmp_validator_load(struct sieve_validator *validator)
{
	unsigned int i;
	pool_t pool = sieve_validator_pool(validator);
	
	struct cmp_validator_context *ctx = 
		p_new(pool, struct cmp_validator_context, 1);
	
	/* Setup address-part registry */
	ctx->registrations = hash_create
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);

	/* Register core address-parts */
	for ( i = 0; i < sieve_core_comparators_count; i++ ) {
		const struct sieve_comparator *cmp = sieve_core_comparators[i];
		
		_sieve_comparator_register(pool, ctx, cmp, -1);
	}

	sieve_validator_extension_set_context(validator, ext_my_id, ctx);

	return TRUE;
}

/*
 * Interpreter context:
 */

struct cmp_interpreter_context {
	ARRAY_DEFINE(cmp_extensions, 
		const struct sieve_comparator_extension *); 
};

static inline struct cmp_interpreter_context *
	get_interpreter_context(struct sieve_interpreter *interpreter)
{
	return (struct cmp_interpreter_context *) 
		sieve_interpreter_extension_get_context(interpreter, ext_my_id);
}

static const struct sieve_comparator_extension *sieve_comparator_extension_get
	(struct sieve_interpreter *interpreter, int ext_id)
{
	struct cmp_interpreter_context *ctx = get_interpreter_context(interpreter);
	
	if ( ext_id > 0 && ext_id < (int) array_count(&ctx->cmp_extensions) ) {
		const struct sieve_comparator_extension * const *ext;

		ext = array_idx(&ctx->cmp_extensions, (unsigned int) ext_id);

		return *ext;
	}
	
	return NULL;
}

void sieve_comparator_extension_set
	(struct sieve_interpreter *interpreter, int ext_id,
		const struct sieve_comparator_extension *ext)
{
	struct cmp_interpreter_context *ctx = get_interpreter_context(interpreter);

	array_idx_set(&ctx->cmp_extensions, (unsigned int) ext_id, &ext);
}

static bool cmp_interpreter_load(struct sieve_interpreter *interpreter)
{
	pool_t pool = sieve_interpreter_pool(interpreter);
	
	struct cmp_interpreter_context *ctx = 
		p_new(pool, struct cmp_interpreter_context, 1);
	
	/* Setup comparator registry */
	p_array_init(&ctx->cmp_extensions, pool, 4);

	sieve_interpreter_extension_set_context(interpreter, ext_my_id, ctx);
	
	return TRUE;
}

/*
 * Comparator operand
 */
 
struct sieve_operand_class comparator_class = { "comparator", NULL };
struct sieve_operand comparator_operand = { "comparator", &comparator_class, FALSE };

/* 
 * Comparator tag 
 */
 
static bool tag_comparator_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd);
static bool tag_comparator_generate
	(struct sieve_generator *generator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd);

const struct sieve_argument comparator_tag = 
	{ "comparator", NULL, tag_comparator_validate, tag_comparator_generate };

static bool tag_comparator_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	int ext_id;
	struct sieve_ast_argument *tag = *arg;
	const struct sieve_comparator *cmp;
	
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check syntax:
	 *   ":comparator" <comparator-name: string>
	 */
	if ( (*arg)->type != SAAT_STRING ) {
		sieve_command_validate_error(validator, cmd, 
			":comparator tag requires one string argument, but %s was found", 
			sieve_ast_argument_name(*arg) );
		return FALSE;
	}
	
	/* Get comparator from registry */
	cmp = sieve_comparator_find
		(validator, sieve_ast_argument_strc(*arg), &ext_id);
	
	if ( cmp == NULL ) {
		sieve_command_validate_error(validator, cmd, 
			"unknown comparator '%s'", sieve_ast_argument_strc(*arg));

		return FALSE;
	}
	
	/* String argument not needed during code generation, so delete it from argument list */
	*arg = sieve_ast_arguments_delete(*arg, 1);

	/* Store comparator in context */
	tag->context = (void *) cmp;
	tag->ext_id = ext_id;
	
	return TRUE;
}

void sieve_comparators_link_tag
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg,	
		unsigned int id_code) 
{
	sieve_validator_register_tag(validator, cmd_reg, &comparator_tag, id_code); 	
}

/* Code generation */

static void opr_comparator_emit
	(struct sieve_binary *sbin, struct sieve_comparator *cmp)
{ 
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_COMPARATOR);
	(void) sieve_binary_emit_byte(sbin, cmp->code);
}

static void opr_comparator_emit_ext
	(struct sieve_binary *sbin, struct sieve_comparator *cmp, int ext_id)
{ 
	unsigned char cmp_code = SIEVE_COMPARATOR_CUSTOM + 
		sieve_binary_extension_get_index(sbin, ext_id);
	
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_COMPARATOR);	
	(void) sieve_binary_emit_byte(sbin, cmp_code);
	if ( cmp->extension->comparator == NULL ) 
		(void) sieve_binary_emit_byte(sbin, cmp->ext_code);
}

const struct sieve_comparator *sieve_opr_comparator_read
  (struct sieve_interpreter *interpreter, 
  	struct sieve_binary *sbin, sieve_size_t *address)
{
	unsigned int cmp_code;
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	
	if ( operand == NULL || operand->class != &comparator_class ) 
		return NULL;
	
	if ( sieve_binary_read_byte(sbin, address, &cmp_code) ) {
		if ( cmp_code < SIEVE_COMPARATOR_CUSTOM ) {
			if ( cmp_code < sieve_core_comparators_count )
				return sieve_core_comparators[cmp_code];
			else
				return NULL;
		} else {
		  int ext_id = -1;
			const struct sieve_comparator_extension *cmp_ext;

			if ( sieve_binary_extension_get_by_index(sbin,
				cmp_code - SIEVE_COMPARATOR_CUSTOM, &ext_id) == NULL )
				return NULL; 

			cmp_ext = sieve_comparator_extension_get(interpreter, ext_id); 
 
			if ( cmp_ext != NULL ) {  	
				unsigned int code;
				if ( cmp_ext->comparator != NULL )
					return cmp_ext->comparator;
		  	
				if ( sieve_binary_read_byte(sbin, address, &code) &&
					cmp_ext->get_comparator != NULL )
					return cmp_ext->get_comparator(code);
			} else {
				i_info("Unknown comparator %d.", cmp_code); 
			}
		}
	}		
		
	return NULL;
}

bool sieve_opr_comparator_dump
	(struct sieve_interpreter *interpreter,
		struct sieve_binary *sbin, sieve_size_t *address)
{
	sieve_size_t pc = *address;
	const struct sieve_comparator *cmp = 
		sieve_opr_comparator_read(interpreter, sbin, address);
	
	if ( cmp == NULL )
		return FALSE;
		
	printf("%08x:   CMP: %s\n", pc, cmp->identifier);
	
	return TRUE;
}

static bool tag_comparator_generate
	(struct sieve_generator *generator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	struct sieve_comparator *cmp = (struct sieve_comparator *) (*arg)->context;
	
	if ( cmp->extension == NULL ) {
		if ( cmp->code < SIEVE_COMPARATOR_CUSTOM )
			opr_comparator_emit(sbin, cmp);
		else
			return FALSE;
	} else {
		opr_comparator_emit_ext(sbin, cmp, (*arg)->ext_id);
	} 
		
	*arg = sieve_ast_argument_next(*arg);
	
	return TRUE;
}

/* 
 * Core comparators  
 */

const struct sieve_comparator i_octet_comparator = {
	"i;octet",
	SIEVE_COMPARATOR_I_OCTET,
	NULL,
	0,
	cmp_i_octet_compare
};

const struct sieve_comparator i_ascii_casemap_comparator = {
	"i;ascii-casemap",
	SIEVE_COMPARATOR_I_ASCII_CASEMAP,
	NULL,
	0,
	cmp_i_ascii_casemap_compare
};

const struct sieve_comparator *sieve_core_comparators[] = {
	&i_octet_comparator, &i_ascii_casemap_comparator
};

const unsigned int sieve_core_comparators_count =
	N_ELEMENTS(sieve_core_comparators);

static int cmp_i_octet_compare(const void *val1, size_t val1_size, 
	const void *val2, size_t val2_size)
{
	int result;

	if ( val1_size == val2_size ) {
		return memcmp(val1, val2, val1_size);
	} 
	
	if ( val1_size > val2_size ) {
		result = memcmp(val1, val2, val2_size);
		
		if ( result == 0 ) return 1;
		
		return result;
	} 

	result = memcmp(val1, val2, val1_size);
		
	if ( result == 0 ) return -1;
		
	return result;
}

static int cmp_i_ascii_casemap_compare(const void *val1, size_t val1_size, const void *val2, size_t val2_size)
{
	int result;

	if ( val1_size == val2_size ) {
		return strncasecmp((const char *) val1, (const char *) val2, val1_size);
	} 
	
	if ( val1_size > val2_size ) {
		result = strncasecmp((const char *) val1, (const char *) val2, val2_size);
		
		if ( result == 0 ) return 1;
		
		return result;
	} 

	result = strncasecmp((const char *) val1, (const char *) val2, val1_size);
		
	if ( result == 0 ) return -1;
		
	return result;
}

