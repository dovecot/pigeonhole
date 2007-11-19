/* Extension relational 
 * --------------------
 *
 * Author: Stephan Bosch
 * Specification: RFC 3431
 * Implementation: validation and generation only
 * Status: under development
 * 
 */

/* Syntax:
 *   MATCH-TYPE =/ COUNT / VALUE
 *   COUNT = ":count" relational-match
 *   VALUE = ":value" relational-match
 *   relational-match = DQUOTE ( "gt" / "ge" / "lt"
 *                             / "le" / "eq" / "ne" ) DQUOTE
 */ 

#include "sieve-common.h"

#include "sieve-ast.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Forward declarations */

static bool ext_relational_load(int ext_id);
static bool ext_relational_validator_load(struct sieve_validator *validator);
static bool ext_relational_interpreter_load
	(struct sieve_interpreter *interpreter);

/* Types */

enum ext_relational_match_type {
  RELATIONAL_VALUE,
  RELATIONAL_COUNT
};

enum relational_match {
	REL_MATCH_GREATER,
	REL_MATCH_GREATER_EQUAL,
	REL_MATCH_LESS,
	REL_MATCH_LESS_EQUAL,
	REL_MATCH_EQUAL,
	REL_MATCH_NOT_EQUAL,
	REL_MATCH_INVALID
};

#define REL_MATCH_INDEX(type, match) \
	(type * REL_MATCH_INVALID + match)

/* Extension definitions */

static int ext_my_id;

const struct sieve_extension relational_extension = { 
	"relational", 
	ext_relational_load,
	ext_relational_validator_load,
	NULL, 
	ext_relational_interpreter_load,  
	NULL, 
	NULL
};

static bool ext_relational_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Validation */

static const struct sieve_match_type rel_match_types[];

static bool ext_relational_parameter_validate
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
	*arg = sieve_ast_arguments_delete(*arg, 1);

	/* Not used just yet */
	ctx->ctx_data = (void *) rel_match;

	/* Override the actual match type with a parameter-specific one */
	ctx->match_type = &rel_match_types
		[REL_MATCH_INDEX(ctx->match_type->ext_code, rel_match)];

	return TRUE;
}

/* Actual extension implementation */


/* Extension access structures */

extern const struct sieve_match_type_extension relational_match_extension;

/* Parameter-independent match type objects, only used during validation */

const struct sieve_match_type value_match_type = {
	"value",
	SIEVE_MATCH_TYPE_CUSTOM,
	&relational_match_extension,
	RELATIONAL_VALUE,
	ext_relational_parameter_validate,
	NULL
};

const struct sieve_match_type count_match_type = {
	"count",
	SIEVE_MATCH_TYPE_CUSTOM,
	&relational_match_extension,
	RELATIONAL_COUNT,
	ext_relational_parameter_validate,
	NULL
};

/* Per-parameter match type objects, used for generation/interpretation 
 * FIXME: This is fast, but kinda hideous.. however, otherwise context data 
 * would have to be passed along with the match type objects everywhere.. also
 * not such a great idea. This needs more thought
 */

#define VALUE_MATCH_TYPE(name, rel_match, func) {     \
		"value-" name,                                    \
		SIEVE_MATCH_TYPE_CUSTOM,                          \
		&relational_match_extension,                      \
		REL_MATCH_INDEX(RELATIONAL_VALUE, rel_match),     \
		NULL,	                                            \
		NULL,                                             \
	}

#define COUNT_MATCH_TYPE(name, rel_match, func) {     \
		"count-" name,                                    \
		SIEVE_MATCH_TYPE_CUSTOM,                          \
		&relational_match_extension,                      \
		REL_MATCH_INDEX(RELATIONAL_COUNT, rel_match),     \
		NULL,	                                            \
		NULL,                                             \
	}
	
static const struct sieve_match_type rel_match_types[] = { 
	VALUE_MATCH_TYPE("gt", REL_MATCH_GREATER, NULL), 
	VALUE_MATCH_TYPE("ge", REL_MATCH_GREATER_EQUAL, NULL), 
	VALUE_MATCH_TYPE("lt", REL_MATCH_LESS, NULL), 
	VALUE_MATCH_TYPE("le", REL_MATCH_LESS_EQUAL, NULL), 
	VALUE_MATCH_TYPE("eq", REL_MATCH_EQUAL, NULL), 
	VALUE_MATCH_TYPE("ne", REL_MATCH_NOT_EQUAL, NULL),

	COUNT_MATCH_TYPE("gt", REL_MATCH_GREATER, NULL), 
	COUNT_MATCH_TYPE("ge", REL_MATCH_GREATER_EQUAL, NULL), 
	COUNT_MATCH_TYPE("lt", REL_MATCH_LESS, NULL), 
	COUNT_MATCH_TYPE("le", REL_MATCH_LESS_EQUAL, NULL), 
	COUNT_MATCH_TYPE("eq", REL_MATCH_EQUAL, NULL), 
	COUNT_MATCH_TYPE("ne", REL_MATCH_NOT_EQUAL, NULL)
};
 
static const struct sieve_match_type *ext_relational_get_match 
	(unsigned int code)
{
	if ( code < N_ELEMENTS(rel_match_types) ) 
		return &rel_match_types[code];
			
	return NULL;
}

const struct sieve_match_type_extension relational_match_extension = { 
	&relational_extension,
	NULL, 
	ext_relational_get_match
};

/* Load extension into validator */

static bool ext_relational_validator_load(struct sieve_validator *validator)
{
	sieve_match_type_register
		(validator, &value_match_type, ext_my_id); 
	sieve_match_type_register
		(validator, &count_match_type, ext_my_id); 

	return TRUE;
}

/* Load extension into interpreter */

static bool ext_relational_interpreter_load
	(struct sieve_interpreter *interpreter)
{
	sieve_match_type_extension_set
		(interpreter, ext_my_id, &relational_match_extension);

	return TRUE;
}


