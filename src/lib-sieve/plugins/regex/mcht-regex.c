/* Match-type ':regex'
 */

#include "lib.h"
#include "mempool.h"
#include "buffer.h"
#include "array.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"

#include "ext-regex-common.h"

#include <sys/types.h>
#include <ctype.h>
#include <regex.h>

/*
 * Configuration
 */

#define MCHT_REGEX_MAX_SUBSTITUTIONS 64
/* 
 * Forward declarations 
 */

void mcht_regex_match_init(struct sieve_match_context *mctx);
static bool mcht_regex_match
	(struct sieve_match_context *mctx, const char *val, size_t val_size,
    	const char *key, size_t key_size, int key_index);
bool mcht_regex_match_deinit(struct sieve_match_context *mctx);

bool mcht_regex_validate_context
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
    struct sieve_match_type_context *ctx, struct sieve_ast_argument *key_arg);

const struct sieve_match_type regex_match_type = {
	SIEVE_OBJECT("regex", &regex_match_type_operand, 0),
	TRUE,
	NULL,
	mcht_regex_validate_context,
	mcht_regex_match_init,
	mcht_regex_match,
	mcht_regex_match_deinit
};

/* 
 * Match-type validation 
 */

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
		errbuf[0] = i_tolower(errbuf[0]);

		buffer_append_space_unsafe(error_buf, errsize);

		return str_c(error_buf);
	}

	return "";
}

static bool mcht_regex_validate_regexp
(struct sieve_validator *validator, struct sieve_match_type_context *ctx,
	struct sieve_ast_argument *key, int cflags) 
{
	int ret;
	regex_t regexp;

	if ( (ret=regcomp(&regexp, sieve_ast_argument_strc(key), cflags)) != 0 ) {
		sieve_command_validate_error(validator, ctx->command_ctx,
			"invalid regular expression for regex match: %s", 
			_regexp_error(&regexp, ret));

		regfree(&regexp);	
		return FALSE;
	}

	regfree(&regexp);
	return TRUE;
}
	
bool mcht_regex_validate_context
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
	struct sieve_match_type_context *ctx, struct sieve_ast_argument *key_arg)
{
	int cflags;
	struct sieve_ast_argument *carg = 
		sieve_command_first_argument(ctx->command_ctx);
	
	cflags =  REG_EXTENDED | REG_NOSUB;
	while ( carg != NULL ) {
		if ( carg != arg && carg->argument == &comparator_tag ) {
			if ( sieve_comparator_tag_is(carg, &i_ascii_casemap_comparator) )
				cflags =  REG_EXTENDED | REG_NOSUB | REG_ICASE;
			else if ( sieve_comparator_tag_is(carg, &i_octet_comparator) )
				cflags =  REG_EXTENDED | REG_NOSUB;
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
		
	/* Validate regular expression(s) */
	if ( sieve_ast_argument_type(key_arg) == SAAT_STRING ) {
		/* Single string */	
		if ( !mcht_regex_validate_regexp(validator, ctx, key_arg, cflags) )
			return FALSE;

	} else if ( sieve_ast_argument_type(key_arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem = sieve_ast_strlist_first(key_arg);

		while ( stritem != NULL ) {
			if ( !mcht_regex_validate_regexp(validator, ctx, stritem, cflags) )
				return FALSE;

			stritem = sieve_ast_strlist_next(stritem);
		}
	} else {
		/* ??? */ 
		sieve_command_validate_error(validator, ctx->command_ctx, 
			"!!BUG!!: mcht_regex_validate_context: invalid ast argument type(%s)",
			sieve_ast_argument_type_name(sieve_ast_argument_type(key_arg)) );
		return FALSE;
	} 

	return TRUE;
}

/* 
 * Match-type implementation 
 */

struct mcht_regex_context {
	ARRAY_DEFINE(reg_expressions, regex_t *);
	int value_index;
	struct sieve_match_values *mvalues;
	regmatch_t *pmatch;
	size_t nmatch;
};

void mcht_regex_match_init
	(struct sieve_match_context *mctx)
{
	struct mcht_regex_context *ctx = 
		t_new(struct mcht_regex_context, 1);
	
	t_array_init(&ctx->reg_expressions, 4);

	ctx->value_index = -1;
	ctx->mvalues = sieve_match_values_start(mctx->interp);
	if ( ctx->mvalues != NULL ) {
		ctx->pmatch = t_new(regmatch_t, MCHT_REGEX_MAX_SUBSTITUTIONS);
		ctx->nmatch = MCHT_REGEX_MAX_SUBSTITUTIONS;
	} else {
		ctx->pmatch = NULL;
		ctx->nmatch = 0;
	}
	
	mctx->data = (void *) ctx;
}

static regex_t *mcht_regex_get
(struct mcht_regex_context *ctx,
	const struct sieve_comparator *cmp, 
	const char *key, unsigned int key_index)
{
	regex_t *regexp = NULL;
	regex_t * const *rxp = &regexp;
	int ret;
	int cflags;
	
	if ( ctx->value_index <= 0 ) {
		regexp = t_new(regex_t, 1);

		if ( cmp == &i_octet_comparator ) 
			cflags =  REG_EXTENDED;
		else if ( cmp ==  &i_ascii_casemap_comparator )
			cflags =  REG_EXTENDED | REG_ICASE;
		else
			return NULL;
			
		if ( ctx->mvalues == NULL ) cflags |= REG_NOSUB;

		if ( (ret=regcomp(regexp, key, cflags)) != 0 ) {
			/* FIXME: Do something useful, i.e. report error somewhere */
			return NULL;
		}

		array_idx_set(&ctx->reg_expressions, key_index, &regexp);
		rxp = &regexp;
	} else {
		rxp = array_idx(&ctx->reg_expressions, key_index);
	}

	return *rxp;
}

static bool mcht_regex_match
(struct sieve_match_context *mctx, 
	const char *val, size_t val_size ATTR_UNUSED, 
	const char *key, size_t key_size ATTR_UNUSED, int key_index)
{
	struct mcht_regex_context *ctx = (struct mcht_regex_context *) mctx->data;
	regex_t *regexp;

	if ( key_index < 0 ) return FALSE;

	if ( key_index == 0 ) ctx->value_index++;

	regexp = mcht_regex_get(ctx, mctx->comparator, key, key_index);
	 
	if ( regexec(regexp, val, ctx->nmatch, ctx->pmatch, 0) == 0 ) {
		size_t i;
		int skipped = 0;
		string_t *subst = t_str_new(32);
		
		for ( i = 0; i < ctx->nmatch; i++ ) {
			str_truncate(subst, 0);
			
			if ( ctx->pmatch[i].rm_so != -1 ) {
				if ( skipped > 0 )
					sieve_match_values_skip(ctx->mvalues, skipped);
					
				str_append_n(subst, val + ctx->pmatch[i].rm_so, 
					ctx->pmatch[i].rm_eo - ctx->pmatch[i].rm_so);
				sieve_match_values_add(ctx->mvalues, subst);
			} else 
				skipped++;
		}
		return TRUE;
	}
	
	return FALSE;
}

bool mcht_regex_match_deinit
	(struct sieve_match_context *mctx)
{
	struct mcht_regex_context *ctx = (struct mcht_regex_context *) mctx->data;
	unsigned int i;

	for ( i = 0; i < array_count(&ctx->reg_expressions); i++ ) {
		regex_t * const *regexp = array_idx(&ctx->reg_expressions, i);

		regfree(*regexp);
	}

	return FALSE;
}

