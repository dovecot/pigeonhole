/* Extension regex 
 * --------------------
 *
 * Author: Stephan Bosch
 * Specification: draft-murchison-sieve-regex-07
 * Implementation: skeleton
 * Status: under development
 * 
 */

#include "lib.h"
#include "mempool.h"
#include "buffer.h"

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"

#include "sieve-comparators.h"
#include "sieve-match-types.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include <sys/types.h>
#include <regex.h>

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
    struct sieve_match_type_context *ctx, struct sieve_ast_argument *key_arg);

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

/* Wrapper around the regerror function for easy access */
static const char *_regexp_error(regex_t *regexp, int errorcode)
{
	size_t errsize = regerror(errorcode, regexp, NULL, 0); 

	if ( errsize > 0 ) {
		char *errbuf;

		buffer_t *error_buf = 
			buffer_create_dynamic(pool_datastack_create(), errsize);
		errbuf = buffer_get_space_unsafe(error_buf, 0, errsize);

		errsize = regerror(errorcode, regexp, errbuf, errsize);
	 
		/* We don't want the error to start with a capital letter */
		errbuf[0] = tolower(errbuf[0]);

		buffer_append_space_unsafe(error_buf, errsize);

		return str_c(error_buf);
	}

	return "";
}

bool mtch_regex_validate_context
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
	struct sieve_match_type_context *ctx, struct sieve_ast_argument *key_arg)
{
	bool result = TRUE;
	int ret, cflags;
	regex_t regexp;
	struct sieve_ast_argument *carg = 
		sieve_command_first_argument(ctx->command_ctx);
	
	cflags =  REG_EXTENDED | REG_NOSUB;
	while ( carg != NULL ) {
		if ( carg != arg && carg->argument == &comparator_tag ) {
			if ( sieve_comparator_tag_is(carg, &i_ascii_casemap_comparator) )
				cflags =  REG_EXTENDED | REG_NOSUB | REG_ICASE;
			else if ( sieve_comparator_tag_is(carg, &i_octet_comparator) )
				cflags =  REG_EXTENDED | REG_NOSUB ;
			else {
				sieve_command_validate_error(validator, ctx->command_ctx, 
					"regex match type only supports "
					"i;octet and i;ascii-casemap comparators" );
				return FALSE;	
			}

			return TRUE;
		}
	
		carg = sieve_ast_argument_next(carg);
	}

	if ( (ret=regcomp(&regexp, sieve_ast_argument_strc(key_arg), cflags)) != 0 ) {
		sieve_command_validate_error(validator, ctx->command_ctx,
			"invalid regular expression for regex match: %s", 
			_regexp_error(&regexp, ret));
		
		result = FALSE;
	}

	regfree(&regexp);

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


