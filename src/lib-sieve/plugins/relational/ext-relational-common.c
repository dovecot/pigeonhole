/* Syntax:
 *   MATCH-TYPE =/ COUNT / VALUE
 *   COUNT = ":count" relational-match
 *   VALUE = ":value" relational-match
 *   relational-match = DQUOTE ( "gt" / "ge" / "lt"
 *                             / "le" / "eq" / "ne" ) DQUOTE
 */ 

#include "lib.h"
#include "str.h"

#include "sieve-common.h"

#include "sieve-ast.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-relational-common.h"

/*
 * Forward declarations
 */

const struct sieve_match_type *rel_match_types[];

/* 
 * Validation 
 */

bool mcht_relational_validate
(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_match_type_context *ctx)
{	
	enum relational_match rel_match = REL_MATCH_INVALID;
	const char *rel_match_id;

	/* Check syntax:
	 *   relational-match = DQUOTE ( "gt" / "ge" / "lt"	
 	 *                             / "le" / "eq" / "ne" ) DQUOTE
 	 *
	 * So, actually this must be a constant string and it is implemented as such 
	 */
	 
	/* Did we get a string in the first place ? */ 
	if ( (*arg)->type != SAAT_STRING ) {
		sieve_command_validate_error(validator, ctx->command_ctx, 
			"the :%s match-type requires a constant string argument being "
			"one of \"gt\", \"ge\", \"lt\", \"le\", \"eq\" or \"ne\", "
			"but %s was found", 
			ctx->match_type->identifier, sieve_ast_argument_name(*arg));
		return FALSE;
	}
	
	/* Check the relational match id */
	
	rel_match_id = sieve_ast_argument_strc(*arg);
	switch ( rel_match_id[0] ) {
	/* "gt" or "ge" */
	case 'g':
		switch ( rel_match_id[1] ) {
		case 't': 
			rel_match = REL_MATCH_GREATER; 
			break;
		case 'e': 
			rel_match = REL_MATCH_GREATER_EQUAL; 
			break;
		default: 
			rel_match = REL_MATCH_INVALID;
		}
		break;
	/* "lt" or "le" */
	case 'l':
		switch ( rel_match_id[1] ) {
		case 't': 
			rel_match = REL_MATCH_LESS; 
			break;
		case 'e': 
			rel_match = REL_MATCH_LESS_EQUAL; 
			break;
		default: 
			rel_match = REL_MATCH_INVALID;
		}
		break;
	/* "eq" */
	case 'e':
		if ( rel_match_id[1] == 'q' )
			rel_match = REL_MATCH_EQUAL;
		else	
			rel_match = REL_MATCH_INVALID;
			
		break;
	/* "ne" */
	case 'n':
		if ( rel_match_id[1] == 'e' )
			rel_match = REL_MATCH_NOT_EQUAL;
		else	
			rel_match = REL_MATCH_INVALID;
		break;
	/* invalid */
	default:
		rel_match = REL_MATCH_INVALID;
	}
	
	if ( rel_match >= REL_MATCH_INVALID ) {
		sieve_command_validate_error(validator, ctx->command_ctx, 
			"the :%s match-type requires a constant string argument being "
			"one of \"gt\", \"ge\", \"lt\", \"le\", \"eq\" or \"ne\", "
			"but \"%s\" was found", 
			ctx->match_type->identifier, rel_match_id);
		return FALSE;
	}
	
	/* Delete argument */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	/* Not used just yet */
	ctx->ctx_data = (void *) rel_match;

	/* Override the actual match type with a parameter-specific one */
	ctx->match_type = rel_match_types
		[REL_MATCH_INDEX(ctx->match_type->code, rel_match)];

	return TRUE;
}

/*
 * Relational match-type operand
 */

const const struct sieve_match_type *rel_match_types[] = {
    &rel_match_value_gt, &rel_match_value_ge, &rel_match_value_lt,
    &rel_match_value_le, &rel_match_value_eq, &rel_match_value_ne,
    &rel_match_count_gt, &rel_match_count_ge, &rel_match_count_lt,
    &rel_match_count_le, &rel_match_count_eq, &rel_match_count_ne
};

static const struct sieve_match_type_operand_interface match_type_operand_intf =
{
    SIEVE_EXT_DEFINE_MATCH_TYPES(rel_match_types)
};

const struct sieve_operand rel_match_type_operand = {
    "relational match",
    &relational_extension,
    0,
    &sieve_match_type_operand_class,
    &match_type_operand_intf
};

