/* Extension comparator-i;ascii-numeric
 * ------------------------------------
 *
 * Author: Stephan Bosch
 * Specification: RFC 2244
 * Implementation: full
 * Status: experimental, largely untested
 * 
 */
 
#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-comparators.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include <ctype.h>

/* Forward declarations */

static bool ext_cmp_i_ascii_numeric_load(int ext_id);
static bool ext_cmp_i_ascii_numeric_validator_load(struct sieve_validator *validator);
static bool ext_cmp_i_ascii_numeric_interpreter_load
	(struct sieve_interpreter *interpreter);

/* Extension definitions */

static int ext_my_id;

const struct sieve_extension comparator_i_ascii_numeric_extension = { 
	"comparator-i;ascii-numeric", 
	ext_cmp_i_ascii_numeric_load,
	ext_cmp_i_ascii_numeric_validator_load,
	NULL, NULL,
	ext_cmp_i_ascii_numeric_interpreter_load,  
	SIEVE_EXT_DEFINE_NO_OPCODES, 
	NULL
};

static bool ext_cmp_i_ascii_numeric_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Actual extension implementation */

/* Extension access structures */

static int cmp_i_ascii_numeric_compare
	(const struct sieve_comparator *cmp, 
		const char *val1, size_t val1_size, const char *val2, size_t val2_size);

extern const struct sieve_comparator_extension i_ascii_numeric_comparator_ext;

const struct sieve_comparator i_ascii_numeric_comparator = { 
	"i;ascii-numeric",
	SIEVE_COMPARATOR_CUSTOM,
	SIEVE_COMPARATOR_FLAG_ORDERING | SIEVE_COMPARATOR_FLAG_EQUALITY,
	&i_ascii_numeric_comparator_ext,
	0,
	cmp_i_ascii_numeric_compare,
	NULL,
	NULL
};

const struct sieve_comparator_extension i_ascii_numeric_comparator_ext = { 
	&comparator_i_ascii_numeric_extension,
	&i_ascii_numeric_comparator, 
	NULL
};

/* Load extension into validator */

static bool ext_cmp_i_ascii_numeric_validator_load(struct sieve_validator *validator)
{
	sieve_comparator_register
		(validator, &i_ascii_numeric_comparator, ext_my_id);
	return TRUE;
}

/* Load extension into interpreter */

static bool ext_cmp_i_ascii_numeric_interpreter_load
	(struct sieve_interpreter *interpreter)
{
	sieve_comparator_extension_set
		(interpreter, ext_my_id, &i_ascii_numeric_comparator_ext);
	return TRUE;
}

/* Implementation */

static int cmp_i_ascii_numeric_compare
	(const struct sieve_comparator *cmp ATTR_UNUSED, 
		const char *val, size_t val_size, const char *key, size_t key_size)
{	
	const char *vend = val + val_size;
	const char *kend = key + key_size;
	const char *vp = val;
	const char *kp = key;
	
	/* Ignore leading zeros */

	while ( *vp == '0' && vp < vend )  
		vp++;

	while ( *kp == '0' && kp < kend )  
		kp++;

	while ( vp < vend && kp < kend ) {
		if ( !isdigit(*vp) || !isdigit(*kp) ) 
			break;

		if ( *vp != *kp ) 
			break;

		vp++;
		kp++;	
	}

	if ( vp == vend || !isdigit(*vp) ) {
		if ( kp == kend || !isdigit(*kp) ) 
			return 0;
		else	
			return -1;
	} else if ( kp == kend || !isdigit(*kp) )  
		return 1;
		
	return (*vp > *kp);
}

