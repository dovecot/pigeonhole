#include "lib.h"
#include "compat.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-comparators.h"

#include <string.h>

/* 
 * Predeclarations 
 */

static int cmp_i_octet_compare(const void *val1, size_t val1_size, const void *val2, size_t val2_size);
static int cmp_i_ascii_casemap_compare(const void *val1, size_t val1_size, const void *val2, size_t val2_size);

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

static void opr_comparator_emit
	(struct sieve_generator *generator, struct sieve_comparator *cmp)
{ 
	(void) sieve_generator_emit_operand(generator, SIEVE_OPERAND_COMPARATOR);
  (void) sieve_generator_emit_byte(generator, (unsigned char) cmp->code);
}

static bool tag_comparator_generate
	(struct sieve_generator *generator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	opr_comparator_emit(generator, (struct sieve_comparator *) (*arg)->context);
		
	*arg = sieve_ast_argument_next(*arg);
	
	return TRUE;
}

/* 
 * Core comparators  
 */

const struct sieve_comparator i_octet_comparator = {
	SCI_I_OCTET,
	"i;octet",
	cmp_i_octet_compare
};

const struct sieve_comparator i_ascii_casemap_comparator = {
	SCI_I_ASCII_CASEMAP,
	"i;ascii-casemap",
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
