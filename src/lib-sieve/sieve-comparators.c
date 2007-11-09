#include "lib.h"
#include "compat.h"

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
 
static struct sieve_interpreter_registry *cmp_registry = NULL;

static void opr_comparator_emit
	(struct sieve_binary *sbin, unsigned int code);
static void opr_comparator_emit_ext
	(struct sieve_binary *sbin, const struct sieve_extension *ext);


static int cmp_i_octet_compare(const void *val1, size_t val1_size, const void *val2, size_t val2_size);
static int cmp_i_ascii_casemap_compare(const void *val1, size_t val1_size, const void *val2, size_t val2_size);

/*
 * Comparator operand
 */
 
struct sieve_operand_class comparator_class = { "comparator", NULL };
struct sieve_operand comparator_operand = { "comparator", &comparator_class };

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
	cmp = sieve_validator_find_comparator(validator, sieve_ast_argument_strc(*arg));
	
	if ( cmp == NULL ) {
		sieve_command_validate_error(validator, cmd, 
			"unknown comparator '%s'", sieve_ast_argument_strc(*arg));

		return FALSE;
	}
	
	/* String argument not needed during code generation, so delete it */
	*arg = sieve_ast_arguments_delete(*arg, 1);
	
	/* Store comparator in context */
	tag->context = (void *) cmp;
	
	return TRUE;
}

/* Code generation */

static void opr_comparator_emit
	(struct sieve_binary *sbin, unsigned int code)
{ 
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_COMPARATOR);
	(void) sieve_binary_emit_byte(sbin, code);
}

static void opr_comparator_emit_ext
	(struct sieve_binary *sbin, const struct sieve_extension *ext)
{ 
	unsigned char cmp_code = SIEVE_COMPARATOR_CUSTOM + 
		sieve_binary_get_extension_index(sbin, ext);
	
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
		  const struct sieve_extension *ext = 
		  	sieve_binary_get_extension(sbin, cmp_code - SIEVE_COMPARATOR_CUSTOM);
		  
		  if ( ext != NULL )
		  	return (const struct sieve_comparator *) 
		  		sieve_interpreter_registry_get(cmp_registry, ext);	
		  else
		  	return NULL;
		}
	}		
		
	return NULL;
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
		opr_comparator_emit_ext(sbin, cmp->extension);
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
	(sizeof(sieve_core_comparators) / sizeof(sieve_core_comparators[0]));


static int cmp_i_octet_compare(const void *val1, size_t val1_size, const void *val2, size_t val2_size)
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

/* 
 * Registry 
 */
 
void sieve_comparators_init_registry(struct sieve_interpreter *interp) 
{
	cmp_registry = sieve_interpreter_registry_init(interp, "comparators");
}


