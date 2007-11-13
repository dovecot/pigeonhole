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
	(struct sieve_binary *sbin, unsigned int code);
static void opr_comparator_emit_ext
	(struct sieve_binary *sbin, int ext_id);

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
 *   name-based comparator registry. 
 */
 
struct cmp_validator_context {
	struct hash_table *comparators;
};

static inline struct cmp_validator_context *
	get_validator_context(struct sieve_validator *validator)
{
	return (struct cmp_validator_context *) 
		sieve_validator_extension_get_context(validator, ext_my_id);
}
 
void sieve_comparator_register
	(struct sieve_validator *validator, const struct sieve_comparator *cmp) 
{
	struct cmp_validator_context *ctx = get_validator_context(validator);
	
	hash_insert(ctx->comparators, (void *) cmp->identifier, (void *) cmp);
}

const struct sieve_comparator *sieve_comparator_find
		(struct sieve_validator *validator, const char *cmp_name) 
{
	struct cmp_validator_context *ctx = get_validator_context(validator);

  return 	(const struct sieve_comparator *) 
  	hash_lookup(ctx->comparators, cmp_name);
}

static bool cmp_validator_load(struct sieve_validator *validator)
{
	unsigned int i;
	pool_t pool = sieve_validator_pool(validator);
	
	struct cmp_validator_context *ctx = p_new(pool, struct cmp_validator_context, 1);
	
	/* Setup comparator registry */
	ctx->comparators = hash_create(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);

	/* Register core comparators */
	for ( i = 0; i < sieve_core_comparators_count; i++ ) {
		const struct sieve_comparator *cmp = sieve_core_comparators[i];
		
		hash_insert(ctx->comparators, (void *) cmp->identifier, (void *) cmp);
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
		return (const struct sieve_comparator_extension *)
			array_idx(&ctx->cmp_extensions, (unsigned int) ext_id);
	}
	
	return NULL;
}

static bool cmp_interpreter_load(struct sieve_interpreter *interpreter)
{
	pool_t pool = sieve_interpreter_pool(interpreter);
	
	struct cmp_interpreter_context *ctx = 
		p_new(pool, struct cmp_interpreter_context, 1);
	
	/* Setup comparator registry */
	p_array_init(&ctx->cmp_extensions, default_pool, 4);

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
	{ "comparator", tag_comparator_validate, tag_comparator_generate };

static bool tag_comparator_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	const struct sieve_comparator *cmp;
	
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check syntax:
	 *   ":comparator" <comparator-name: string>
	 */
	if ( (*arg)->type != SAAT_STRING ) {
		sieve_command_validate_error(validator, cmd, 
			":comparator tag requires one string argument, but %s was found", sieve_ast_argument_name(*arg) );
		return FALSE;
	}
	
	/* Get comparator from registry */
	cmp = sieve_comparator_find(validator, sieve_ast_argument_strc(*arg));
	
	if ( cmp == NULL ) {
		sieve_command_validate_error(validator, cmd, 
			"unknown comparator '%s'", sieve_ast_argument_strc(*arg));

		return FALSE;
	}
	
	/* String argument not needed during code generation, so delete it from argument list */
	*arg = sieve_ast_arguments_delete(*arg, 1);

	/* Store comparator in context */
	tag->context = (void *) cmp;
	
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
	(struct sieve_binary *sbin, unsigned int code)
{ 
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_COMPARATOR);
	(void) sieve_binary_emit_byte(sbin, code);
}

static void opr_comparator_emit_ext
	(struct sieve_binary *sbin, int ext_id)
{ 
	unsigned char cmp_code = SIEVE_COMPARATOR_CUSTOM + 
		sieve_binary_extension_get_index(sbin, ext_id);
	
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_COMPARATOR);	
	(void) sieve_binary_emit_byte(sbin, cmp_code);
}

const struct sieve_comparator *sieve_opr_comparator_read
  (struct sieve_binary *sbin, sieve_size_t *address)
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
		  /*int ext_id = -1;
		  const struct sieve_extension *ext = 
		  	sieve_binary_extension_get_by_index
		  		(sbin, cmp_code - SIEVE_COMPARATOR_CUSTOM, &ext_id);
		  
		  if ( ext != NULL )
		  	struct sieve_comparator_extension *cext = 
		  		sieve_comparator_extension_get(cmp_registry, ext);	
		  else*/
		  	return NULL;
		}
	}		
		
	return NULL;
}

bool sieve_opr_comparator_dump(struct sieve_binary *sbin, sieve_size_t *address)
{
	sieve_size_t pc = *address;
	const struct sieve_comparator *cmp = sieve_opr_comparator_read(sbin, address);
	
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
			opr_comparator_emit(sbin, cmp->code);
		else
			return FALSE;
	} else {
		//opr_comparator_emit_ext(sbin, cmp->ext);
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
	cmp_i_octet_compare
};

const struct sieve_comparator i_ascii_casemap_comparator = {
	"i;ascii-casemap",
	SIEVE_COMPARATOR_I_ASCII_CASEMAP,
	NULL,
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

