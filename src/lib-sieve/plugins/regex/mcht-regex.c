/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

/* Match-type ':regex'
 */

#include "lib.h"
#include "mempool.h"
#include "buffer.h"
#include "array.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-match.h"

#include "ext-regex-common.h"

#include <sys/types.h>
#include <regex.h>
#include <ctype.h>

/*
 * Configuration
 */

#define MCHT_REGEX_MAX_SUBSTITUTIONS SIEVE_MAX_MATCH_VALUES

/* 
 * Match type
 */
 
bool mcht_regex_validate_context
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
    struct sieve_match_type_context *ctx, struct sieve_ast_argument *key_arg);

static void mcht_regex_match_init(struct sieve_match_context *mctx);
static int mcht_regex_match
	(struct sieve_match_context *mctx, const char *val, size_t val_size,
    	const char *key, size_t key_size, int key_index);
static int mcht_regex_match_deinit(struct sieve_match_context *mctx);

const struct sieve_match_type regex_match_type = {
	SIEVE_OBJECT("regex", &regex_match_type_operand, 0),
	TRUE, FALSE,
	NULL,
	mcht_regex_validate_context,
	mcht_regex_match_init,
	mcht_regex_match,
	mcht_regex_match_deinit
};

/* 
 * Match type validation 
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

static int mcht_regex_validate_regexp
(struct sieve_validator *validator, 
	struct sieve_match_type_context *ctx ATTR_UNUSED,
	struct sieve_ast_argument *key, int cflags) 
{
	int ret;
	regex_t regexp;

	if ( (ret=regcomp(&regexp, sieve_ast_argument_strc(key), cflags)) != 0 ) {
		sieve_argument_validate_error(validator, key,
			"invalid regular expression for regex match: %s", 
			_regexp_error(&regexp, ret));

		regfree(&regexp);	
		return FALSE;
	}

	regfree(&regexp);
	return TRUE;
}

struct _regex_key_context {
	struct sieve_validator *valdtr;
	struct sieve_match_type_context *mctx;
	int cflags;
};

static int mcht_regex_validate_key_argument
(void *context, struct sieve_ast_argument *key)
{
	struct _regex_key_context *keyctx = (struct _regex_key_context *) context;

	/* FIXME: We can currently only handle string literal argument, so
	 * variables are not allowed.
	 */
	if ( !sieve_argument_is_string_literal(key) ) {
		sieve_argument_validate_error(keyctx->valdtr, key,
			"this Sieve implementation currently only accepts a literal string "
			"for a regular expression");
		return FALSE;
	}

	return mcht_regex_validate_regexp
		(keyctx->valdtr, keyctx->mctx, key, keyctx->cflags);
}
	
bool mcht_regex_validate_context
(struct sieve_validator *validator, struct sieve_ast_argument *arg ATTR_UNUSED,
	struct sieve_match_type_context *ctx, struct sieve_ast_argument *key_arg)
{
	const struct sieve_comparator *cmp = ctx->comparator;
	int cflags = REG_EXTENDED | REG_NOSUB;
	struct _regex_key_context keyctx;
	struct sieve_ast_argument *kitem;

	if ( cmp != NULL ) { 
		if ( cmp == &i_ascii_casemap_comparator )
			cflags =  REG_EXTENDED | REG_NOSUB | REG_ICASE;
		else if ( cmp == &i_octet_comparator )
			cflags =  REG_EXTENDED | REG_NOSUB;
		else {
			sieve_argument_validate_error(validator, ctx->match_type_arg, 
				"regex match type only supports "
				"i;octet and i;ascii-casemap comparators" );
			return FALSE;	
		}
	}

	/* Validate regular expression keys */

	keyctx.valdtr = validator;
	keyctx.mctx = ctx;
	keyctx.cflags = cflags;

	kitem = key_arg;
	if ( !sieve_ast_stringlist_map(&kitem, (void *) &keyctx,
		mcht_regex_validate_key_argument) )
		return FALSE;

	return TRUE;
}

/* 
 * Match type implementation 
 */

struct mcht_regex_context {
	ARRAY_DEFINE(reg_expressions, regex_t *);
	int value_index;
	regmatch_t *pmatch;
	size_t nmatch;
};

static void mcht_regex_match_init
	(struct sieve_match_context *mctx)
{
	struct mcht_regex_context *ctx = 
		t_new(struct mcht_regex_context, 1);
	
	t_array_init(&ctx->reg_expressions, 4);

	ctx->value_index = -1;
	if ( sieve_match_values_are_enabled(mctx->interp) ) {
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
			
		if ( ctx->nmatch == 0 ) cflags |= REG_NOSUB;

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

static int mcht_regex_match
(struct sieve_match_context *mctx, 
	const char *val, size_t val_size ATTR_UNUSED, 
	const char *key, size_t key_size ATTR_UNUSED, int key_index)
{
	struct mcht_regex_context *ctx = (struct mcht_regex_context *) mctx->data;
	regex_t *regexp;

	if ( val == NULL ) {
		val = "";
		val_size = 0;
	}

	if ( key_index < 0 ) return FALSE;

	if ( key_index == 0 ) ctx->value_index++;

	if ( (regexp=mcht_regex_get(ctx, mctx->comparator, key, key_index)) == NULL )
		return FALSE;

	if ( regexec(regexp, val, ctx->nmatch, ctx->pmatch, 0) == 0 ) {

		if ( ctx->nmatch > 0 ) {
			size_t i;
			int skipped = 0;
			string_t *subst = t_str_new(32);

			struct sieve_match_values *mvalues = sieve_match_values_start(mctx->interp);

			i_assert( mvalues != NULL );

			for ( i = 0; i < ctx->nmatch; i++ ) {
				str_truncate(subst, 0);
			
				if ( ctx->pmatch[i].rm_so != -1 ) {
					if ( skipped > 0 )
						sieve_match_values_skip(mvalues, skipped);
					
					str_append_n(subst, val + ctx->pmatch[i].rm_so, 
						ctx->pmatch[i].rm_eo - ctx->pmatch[i].rm_so);
					sieve_match_values_add(mvalues, subst);
				} else 
					skipped++;
			}

			sieve_match_values_commit(mctx->interp, &mvalues);
		}

		return TRUE;
	}
	
	return FALSE;
}

int mcht_regex_match_deinit
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

