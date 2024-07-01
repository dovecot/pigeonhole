/* Copyright (c) 2025 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-ast.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-stringlist.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-match.h"

#include "ext-extlists-common.h"

/*
 * Match-type objects
 */

static bool
mcht_list_validate_context(struct sieve_validator *valdtr,
			   struct sieve_ast_argument *arg,
			   struct sieve_match_type_context *ctx,
			   struct sieve_ast_argument *key_arg);

static int
match_list_match(struct sieve_match_context *mctx,
		 struct sieve_stringlist *value_list,
		 struct sieve_stringlist *key_list);

const struct sieve_match_type_def list_match_type = {
	SIEVE_OBJECT("list", &list_match_type_operand, 0),
	.validate_context = mcht_list_validate_context,
	.match = match_list_match,
};

/*
 * Validation
 */

static bool
mcht_list_validate_context(struct sieve_validator *valdtr,
			   struct sieve_ast_argument *arg,
			   struct sieve_match_type_context *mtctx,
			   struct sieve_ast_argument *key_arg ATTR_UNUSED)
{
	if (mtctx->comparator_specified) {
		sieve_argument_validate_error(
			valdtr, arg,
			"the :%s match type does not allow a comparator",
			mtctx->match_type->object.def->identifier );
		return FALSE;
	}
	return TRUE;
}

/*
 * Match-type implementation
 */

static int
match_list_match(struct sieve_match_context *mctx,
		 struct sieve_stringlist *value_list,
		 struct sieve_stringlist *key_list)
{
	const struct sieve_runtime_env *renv = mctx->runenv;
	const struct sieve_extension *ext =
		SIEVE_OBJECT_EXTENSION(mctx->match_type);
	struct ext_extlists_context *extctx = ext->context;
	bool match_enabled = sieve_match_values_are_enabled(renv);
	const char *match = NULL;
	bool found = FALSE;

	mctx->exec_status = ext_extlists_lookup(renv, extctx,
						value_list, key_list,
						(match_enabled ? &match : NULL),
						&found);
	if (mctx->exec_status != SIEVE_EXEC_OK)
		return -1;

	if (found) {
		if (match_enabled) {
			struct sieve_match_values *mvalues;

			i_assert(match != NULL);
			mvalues = sieve_match_values_start(mctx->runenv);
			sieve_match_values_add_cstr(mvalues, match);
			sieve_match_values_commit(mctx->runenv, &mvalues);
		}
		return 1;
	}
	return 0;
}
