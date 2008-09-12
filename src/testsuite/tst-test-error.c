/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-script.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"
#include "sieve-match.h"

#include "testsuite-common.h"

/*
 * Test_error command
 *
 * Syntax:   
 *   test [MATCH-TYPE] [COMPARATOR] [:index number] <key-list: string-list>
 */

static bool tst_test_error_registered
    (struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_test_error_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool tst_test_error_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command tst_test_error = { 
	"test_error", 
	SCT_TEST, 
	1, 0, FALSE, FALSE,
	tst_test_error_registered, 
	NULL,
	tst_test_error_validate, 
	tst_test_error_generate, 
	NULL 
};

/* 
 * Operation 
 */

static bool tst_test_error_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_test_error_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation test_error_operation = { 
	"TEST_ERROR",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_ERROR,
	tst_test_error_operation_dump, 
	tst_test_error_operation_execute 
};

/*
 * Tagged arguments
 */ 

/* NOTE: This will be merged with the date-index extension when it is 
 * implemented.
 */

static bool tst_test_error_validate_index_tag
	(struct sieve_validator *validator, struct sieve_ast_argument **arg,
		struct sieve_command_context *cmd);

static const struct sieve_argument test_error_index_tag = {
    "index",
    NULL, NULL,
    tst_test_error_validate_index_tag,
    NULL, NULL
};

enum tst_test_error_optional {
	OPT_INDEX = SIEVE_MATCH_OPT_LAST,
};


/*
 * Argument implementation
 */

static bool tst_test_error_validate_index_tag
(struct sieve_validator *validator, struct sieve_ast_argument **arg,
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Check syntax:
	 *   :index number
	 */
	if ( !sieve_validate_tag_parameter
		(validator, cmd, tag, *arg, SAAT_NUMBER) ) {
		return FALSE;
	}

    /* Skip parameter */
    *arg = sieve_ast_argument_next(*arg);
    return TRUE;
}


/*
 * Command registration
 */

static bool tst_test_error_registered
(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg)
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(validator, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);

	 sieve_validator_register_tag
        (validator, cmd_reg, &test_error_index_tag, OPT_INDEX);

	return TRUE;
}

/* 
 * Validation 
 */

static bool tst_test_error_validate
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_command_context *tst) 
{
	struct sieve_ast_argument *arg = tst->first_positional;
	
	if ( !sieve_validate_positional_argument
        (valdtr, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
        return FALSE;
    }

    if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
        return FALSE;

    /* Validate the key argument to a specified match type */
    return sieve_match_type_validate
		(valdtr, tst, arg, &is_match_type, &i_octet_comparator);
}

/* 
 * Code generation 
 */

static inline struct testsuite_generator_context *
_get_generator_context(struct sieve_generator *gentr)
{
	return (struct testsuite_generator_context *) 
		sieve_generator_extension_get_context(gentr, &testsuite_extension);
}

static bool tst_test_error_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *tst)
{
	sieve_operation_emit_code(cgenv->sbin, &test_error_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

/* 
 * Code dump
 */
 
static bool tst_test_error_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "TEST_ERROR:");
	sieve_code_descend(denv);

	/* Handle any optional arguments */
	do {
		if ( !sieve_match_dump_optional_operands(denv, address, &opt_code) )
			return FALSE;

		switch ( opt_code ) {
		case SIEVE_MATCH_OPT_END:
			break;
		case OPT_INDEX:
			if ( !sieve_opr_number_dump(denv, address, "index") )
				return FALSE;
			break;
		default:
			return FALSE;
		}
	} while ( opt_code != SIEVE_MATCH_OPT_END );

	return sieve_opr_stringlist_dump(denv, address, "key list");
}

/*
 * Intepretation
 */

static int tst_test_error_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	int opt_code = 0;
	bool result = TRUE;
	const struct sieve_comparator *cmp = &i_octet_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *key_list;
	bool matched;
	const char *error;
	int cur_index = 0, index = 0;
	int ret;

	/*
	 * Read operands
	 */

	/* Handle optional operands */
	do {
		sieve_number_t number; 

		if ( (ret=sieve_match_read_optional_operands
			(renv, address, &opt_code, &cmp, &mtch)) <= 0 )
 			return ret;

		switch ( opt_code ) {
		case SIEVE_MATCH_OPT_END:
			break;
		case OPT_INDEX:
			if ( !sieve_opr_number_read(renv, address, &number) ) {
				sieve_runtime_trace_error(renv, "invalid index operand");
				return SIEVE_EXEC_BIN_CORRUPT;
			}
			index = (int) number;
			break;
		default:
			sieve_runtime_trace_error(renv, "invalid optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}	
	} while ( opt_code != SIEVE_MATCH_OPT_END);

	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid key-list operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/*
	 * Perform operation
	 */
	
	sieve_runtime_trace(renv, "TEST_ERROR test (index: %d)", index);

	testsuite_script_get_error_init();

    /* Initialize match */
    mctx = sieve_match_begin(renv->interp, mtch, cmp, NULL, key_list);

    /* Iterate through all errors to match */
	error = NULL;
	matched = FALSE;
	cur_index = 1;
	ret = 0;
	while ( result && !matched &&
		(error=testsuite_script_get_error_next(FALSE)) != NULL ) {
		
		if ( index == 0 || index == cur_index ) {
			if ( (ret=sieve_match_value(mctx, error, strlen(error))) < 0 ) {
				result = FALSE;
				break;
			}
		}

		matched = ret > 0;
		cur_index++;
    }

    /* Finish match */
    if ( (ret=sieve_match_end(mctx)) < 0 )
        result = FALSE;
    else
        matched = ( ret > 0 || matched );

	/* Set test result for subsequent conditional jump */
    if ( result ) {
        sieve_interpreter_set_test_result(renv->interp, matched);
        return SIEVE_EXEC_OK;
    }

    sieve_runtime_trace_error(renv, "invalid string-list item");
    return SIEVE_EXEC_BIN_CORRUPT;
}




