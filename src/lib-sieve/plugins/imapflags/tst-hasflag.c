#include "lib.h"

#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

#include "ext-imapflags-common.h"

/* Forward declarations */

static bool tst_hasflag_registered
	(struct sieve_validator *validator,
		struct sieve_command_registration *cmd_reg);
static bool tst_hasflag_validate
	(struct sieve_validator *validator,	struct sieve_command_context *ctx);
static bool tst_hasflag_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

static bool tst_hasflag_opcode_dump
	(const struct sieve_opcode *opcode,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool tst_hasflag_opcode_execute
	(const struct sieve_opcode *opcode,	
		const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Hasflag test
 *
 * Syntax: 
 *   hasflag [MATCH-TYPE] [COMPARATOR] [<variable-list: string-list>]
 *       <list-of-flags: string-list>
 */
 
const struct sieve_command tst_hasflag = { 
	"hasflag", 
	SCT_TEST,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE, 
	tst_hasflag_registered, 
	NULL,
	tst_hasflag_validate, 
	tst_hasflag_generate, 
	NULL 
};

/* Hasflag opcode */

const struct sieve_opcode hasflag_opcode = { 
	"HASFLAG",
	SIEVE_OPCODE_CUSTOM,
	&imapflags_extension,
	EXT_IMAPFLAGS_OPCODE_HASFLAG,
	tst_hasflag_opcode_dump,
	tst_hasflag_opcode_execute
};

/* Optional arguments */
#include "sieve-comparators.h"
#include "sieve-match-types.h"
enum tst_hasflag_optional {	
	OPT_END,
	OPT_COMPARATOR,
	OPT_MATCH_TYPE
};

/* Tag registration */

static bool tst_hasflag_registered
(struct sieve_validator *validator, 
	struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, OPT_COMPARATOR);
	sieve_match_types_link_tags(validator, cmd_reg, OPT_MATCH_TYPE);

	return TRUE;
}

/* Validation */

static bool tst_hasflag_validate
	(struct sieve_validator *validator,	struct sieve_command_context *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_ast_argument *arg2;
	
	/* First validate arguments */
		
	if ( arg == NULL ) {
		sieve_command_validate_error(validator, tst, 
			"the hasflag test expects at least one argument, but none was found");
		return FALSE;
	}
	
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && 
		sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) 
	{
		sieve_command_validate_error(validator, tst, 
			"the hasflag test expects a string-list (variable list or list of flags) "
			"as first argument, but %s was found", sieve_ast_argument_name(arg));
		return FALSE; 
	}
	sieve_validator_argument_activate(validator, arg);
	
	arg2 = sieve_ast_argument_next(arg);
	if ( arg2 != NULL ) {
		/* First, check syntax sanity */
		
		if ( sieve_ast_argument_type(arg2) != SAAT_STRING && 
			sieve_ast_argument_type(arg2) != SAAT_STRING_LIST ) 
		{
			sieve_command_validate_error(validator, tst, 
				"the hasflag test expects a string list (list of flags) as "
				"second argument when two arguments are specified, "
				"but %s was found",
				sieve_ast_argument_name(arg2));
			return FALSE; 
		}
		
		/* Then, check whether the second argument is permitted */
		
		/* IF !VARIABLE EXTENSION LOADED */
		{
			sieve_command_validate_error(validator, tst, 
				"the hasflag test only allows for the specification of a "
				"variable list when the variables extension is active");
			return FALSE;
		}
	
		sieve_validator_argument_activate(validator, arg2);
	} else 
		arg2 = arg;
	
	/* Validate the key argument to a specified match type */
	
  sieve_match_type_validate(validator, tst, arg2);
	
	return TRUE;
}

/*
 * Code generation 
 */

static bool tst_hasflag_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx)
{
	sieve_generator_emit_opcode_ext
		(generator, &hasflag_opcode, ext_imapflags_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/* 
 * Code dump 
 */
 
static bool tst_hasflag_opcode_dump
(const struct sieve_opcode *opcode ATTR_UNUSED,	
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 1;

	printf("HASFLAG\n");

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(denv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(denv->sbin, address, &opt_code) ) 
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_COMPARATOR:
				sieve_opr_comparator_dump(denv, address);
				break;
			case OPT_MATCH_TYPE:
				sieve_opr_match_type_dump(denv, address);
				break;
			default: 
				return FALSE;
			}
 		}
	}

	return
		sieve_opr_stringlist_dump(denv, address);
}

/*
 * Execution
 */

static bool tst_hasflag_opcode_execute
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	int opt_code = 1;
	const struct sieve_comparator *cmp = &i_ascii_casemap_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *key_list;
	struct ext_imapflags_iter iter;
	const char *flag;
	bool matched;
	
	printf("?? HASFLAG\n");

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) )
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_COMPARATOR:
				cmp = sieve_opr_comparator_read(renv->sbin, address);
				break;
			case OPT_MATCH_TYPE:
				mtch = sieve_opr_match_type_read(renv->sbin, address);
				break;
			default:
				return FALSE;
			}
		}
	}

	t_push();
		
	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv->sbin, address)) == NULL ) {
		t_pop();
		return FALSE;
	}

	matched = FALSE;
	mctx = sieve_match_begin(mtch, cmp, key_list); 	

	ext_imapflags_get_flags_init(&iter, renv->interp);
	
	while ( (flag=ext_imapflags_iter_get_flag(&iter)) != NULL ) {
		if ( sieve_match_value(mctx, flag) )
			matched = TRUE; 	
	}

	matched = sieve_match_end(mctx) || matched; 	
	
	t_pop();
	
	sieve_interpreter_set_test_result(renv->interp, matched);
	
	return TRUE;
}


