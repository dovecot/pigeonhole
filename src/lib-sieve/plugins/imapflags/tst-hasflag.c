#include "lib.h"

#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-imapflags-common.h"

/* Forward declarations */

static bool tst_hasflag_registered
	(struct sieve_validator *validator,
		struct sieve_command_registration *cmd_reg);
static bool tst_hasflag_validate
	(struct sieve_validator *validator,	struct sieve_command_context *ctx);
static bool tst_hasflag_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

static bool tst_hasflag_operation_dump
	(const struct sieve_operation *op,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool tst_hasflag_operation_execute
	(const struct sieve_operation *op,	
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

/* Hasflag operation */

const struct sieve_operation hasflag_operation = { 
	"HASFLAG",
	&imapflags_extension,
	EXT_IMAPFLAGS_OPERATION_HASFLAG,
	tst_hasflag_operation_dump,
	tst_hasflag_operation_execute
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
	if ( !ext_imapflags_command_validate(validator, tst) )
		return FALSE;
		
	/* Validate the key argument to a specified match type */
	
	return sieve_match_type_validate(validator, tst, 
		sieve_ast_argument_next(tst->first_positional));
}

/*
 * Code generation 
 */

static bool tst_hasflag_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
	sieve_operation_emit_code(cgenv->sbin, &hasflag_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/* 
 * Code dump 
 */
 
static bool tst_hasflag_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,	
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 1;

	sieve_code_dumpf(denv, "HASFLAG");
	sieve_code_descend(denv);

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

	return ext_imapflags_command_operands_dump(denv, address); 
}

/*
 * Execution
 */

static bool tst_hasflag_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	int opt_code = 1;
	const struct sieve_comparator *cmp = &i_ascii_casemap_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *flag_list;
	struct sieve_variable_storage *storage;
	unsigned int var_index;
	struct ext_imapflags_iter iter;
	const char *flag;
	bool matched;
	
	sieve_runtime_trace(renv, "HASFLAG test");

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) )
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_COMPARATOR:
				cmp = sieve_opr_comparator_read(renv, address);
				break;
			case OPT_MATCH_TYPE:
				mtch = sieve_opr_match_type_read(renv, address);
				break;
			default:
				return FALSE;
			}
		}
	}

	if ( !ext_imapflags_command_operands_read
		(renv, address, &flag_list, &storage, &var_index) )
		return FALSE;

	matched = FALSE;
	mctx = sieve_match_begin(renv->interp, mtch, cmp, flag_list); 	

	ext_imapflags_get_flags_init(&iter, renv, storage, var_index);
	
	while ( (flag=ext_imapflags_iter_get_flag(&iter)) != NULL ) {
		if ( sieve_match_value(mctx, flag, strlen(flag)) )
			matched = TRUE; 	
	}

	matched = sieve_match_end(mctx) || matched; 	
	
	sieve_interpreter_set_test_result(renv->interp, matched);
	
	return TRUE;
}


