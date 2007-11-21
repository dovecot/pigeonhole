/* Extension comparator-i;ascii-numeric
 * ------------------------------------
 *
 * Author: Stephan Bosch
 * Specification: RFC 2244
 * Implementation: full, but fails to handle leading zeros.
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
	NULL, 
	ext_cmp_i_ascii_numeric_interpreter_load,  
	NULL, 
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
		const char *val1, size_t val1_size, const char *val2, size_t val2_size)
{
	unsigned int i = 0;
	int result = 0;
	const char *nval1 = (const char *) val1, *nval2 = (const char *) val2;

	while ( i < val1_size && i < val2_size ) {	
		if ( isdigit(nval1[i]) )  {
			if ( isdigit(nval2[i]) ) {
				if ( result == 0 && nval1[i] != nval2[i] ) { 
					if ( nval1[i] > nval2[i] )
						result = 1;
					else
						result = 0;
				}
			} else {
				return 1;
			}
		} else {
			if ( isdigit(nval2[i]) ) {
				return -1;
			} else {
				return result;
			}
		}
				
		i++;
	}
		
	return result;
}

