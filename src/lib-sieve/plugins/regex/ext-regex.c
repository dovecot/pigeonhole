/* Extension regex 
 * --------------------
 *
 * Author: Stephan Bosch
 * Specification: draft-murchison-sieve-regex-07
 * Implementation: skeleton
 * Status: under development
 * 
 */

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"

#include "sieve-comparators.h"
#include "sieve-match-types.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Forward declarations */

static bool ext_regex_load(int ext_id);
static bool ext_regex_validator_load(struct sieve_validator *validator);
static bool ext_regex_interpreter_load
	(struct sieve_interpreter *interpreter);

/* Extension definitions */

static int ext_my_id;

const struct sieve_extension regex_extension = { 
	"regex", 
	ext_regex_load,
	ext_regex_validator_load,
	NULL, 
	ext_regex_interpreter_load,  
	NULL, 
	NULL
};

static bool ext_regex_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Actual extension implementation */


/* Extension access structures */

extern const struct sieve_match_type_extension regex_match_extension;

bool mtch_regex_validate_context
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
    struct sieve_match_type_context *ctx);

const struct sieve_match_type regex_match_type = {
	"regex",
	SIEVE_MATCH_TYPE_CUSTOM,
	&regex_match_extension,
	0,
	NULL,
	mtch_regex_validate_context,
	NULL
};

const struct sieve_match_type_extension regex_match_extension = { 
	&regex_extension,
	&regex_match_type, 
	NULL
};

/* Validation */

bool mtch_regex_validate_context
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
	struct sieve_match_type_context *ctx)
{
	struct sieve_ast_argument *carg = 
		sieve_command_first_argument(ctx->command_ctx);

	while ( carg != NULL ) {
		if ( carg != arg && carg->argument == &comparator_tag ) {
			if (!sieve_comparator_tag_is(carg, &i_ascii_casemap_comparator) &&
				!sieve_comparator_tag_is(carg, &i_octet_comparator) )
			{
				sieve_command_validate_error(validator, ctx->command_ctx, 
					"regex match type only supports i;octet and i;ascii-casemap comparators" );
				return FALSE;	
			}

			return TRUE;
		}
	
		carg = sieve_ast_argument_next(carg);
	}

	return TRUE;
}

/* Load extension into validator */

static bool ext_regex_validator_load(struct sieve_validator *validator)
{
	sieve_match_type_register
		(validator, &regex_match_type, ext_my_id); 

	return TRUE;
}

/* Load extension into interpreter */

static bool ext_regex_interpreter_load
	(struct sieve_interpreter *interpreter)
{
	sieve_match_type_extension_set
		(interpreter, ext_my_id, &regex_match_extension);

	return TRUE;
}


