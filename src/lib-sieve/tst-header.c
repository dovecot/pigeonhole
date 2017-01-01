/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

/*
 * Header test
 *
 * Syntax:
 *   header [COMPARATOR] [MATCH-TYPE]
 *     <header-names: string-list> <key-list: string-list>
 */

static bool tst_header_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool tst_header_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_header_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *tst);

const struct sieve_command_def tst_header = {
	.identifier = "header",
	.type = SCT_TEST,
	.positional_args = 2,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = tst_header_registered,
	.validate = tst_header_validate,
	.generate = tst_header_generate
};

/*
 * Header operation
 */

static bool tst_header_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_header_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def tst_header_operation = {
	.mnemonic = "HEADER",
	.code = SIEVE_OPERATION_HEADER,
	.dump = tst_header_operation_dump,
	.execute = tst_header_operation_execute
};

/*
 * Test registration
 */

static bool tst_header_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_command_registration *cmd_reg)
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(valdtr, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);

	return TRUE;
}

/*
 * Validation
 */

static bool tst_header_validate
(struct sieve_validator *valdtr, struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_comparator cmp_default =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	struct sieve_match_type mcht_default =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);

	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "header names", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	if ( !sieve_command_verify_headers_argument(valdtr, arg) )
		return FALSE;

	arg = sieve_ast_argument_next(arg);

	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(valdtr, tst, arg, &mcht_default, &cmp_default);
}

/*
 * Code generation
 */

static bool tst_header_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *tst)
{
	sieve_operation_emit(cgenv->sblock, NULL, &tst_header_operation);

 	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

/*
 * Code dump
 */

static bool tst_header_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "HEADER");
	sieve_code_descend(denv);

	/* Optional operands */
	if ( sieve_message_opr_optional_dump(denv, address, NULL) != 0 )
		return FALSE;

	return
		sieve_opr_stringlist_dump(denv, address, "header names") &&
		sieve_opr_stringlist_dump(denv, address, "key list");
}

/*
 * Code execution
 */

static int tst_header_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_comparator cmp =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	struct sieve_match_type mcht =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	struct sieve_stringlist *hdr_list, *key_list, *value_list;
	ARRAY_TYPE(sieve_message_override) svmos;
	int match, ret;

	/*
	 * Read operands
	 */

	/* Optional operands */
	i_zero(&svmos);
	if ( sieve_message_opr_optional_read
		(renv, address, NULL, &ret, NULL, &mcht, &cmp, &svmos) < 0 )
		return ret;

	/* Read header-list */
	if ( (ret=sieve_opr_stringlist_read(renv, address, "header-list", &hdr_list))
		<= 0 )
		return ret;

	/* Read key-list */
	if ( (ret=sieve_opr_stringlist_read(renv, address, "key-list", &key_list))
		<= 0 )
		return ret;

	/*
	 * Perform test
	 */

	sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "header test");

	/* Get header */
	sieve_runtime_trace_descend(renv);
	if ( (ret=sieve_message_get_header_fields
		(renv, hdr_list, &svmos, TRUE, &value_list)) <= 0 )
		return ret;
	sieve_runtime_trace_ascend(renv);

	/* Perform match */
	if ( (match=sieve_match(renv, &mcht, &cmp, value_list, key_list, &ret)) < 0 )
		return ret;

	/* Set test result for subsequent conditional jump */
	sieve_interpreter_set_test_result(renv->interp, match > 0);
	return SIEVE_EXEC_OK;
}
