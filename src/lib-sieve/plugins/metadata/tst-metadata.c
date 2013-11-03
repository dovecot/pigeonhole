/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

#include "ext-metadata-common.h"

/*
 * Test definitions
 */

/* Forward declarations */

static bool tst_metadata_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool tst_metadata_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_metadata_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd);

/* Metadata test
 *
 * Syntax:
 *   metadata [MATCH-TYPE] [COMPARATOR]
 *            <mailbox: string>
 *            <annotation-name: string> <key-list: string-list>
 */

const struct sieve_command_def metadata_test = {
	"metadata",
	SCT_TEST,
	3, 0, FALSE, FALSE,
	tst_metadata_registered,
	NULL,
	tst_metadata_validate,
	NULL,
	tst_metadata_generate,
	NULL
};

/* Servermetadata test
 *
 * Syntax:
 *   servermetadata [MATCH-TYPE] [COMPARATOR]
 *            <annotation-name: string> <key-list: string-list>
 */

const struct sieve_command_def servermetadata_test = {
	"servermetadata",
	SCT_TEST,
	2, 0, FALSE, FALSE,
	tst_metadata_registered,
	NULL,
	tst_metadata_validate,
	NULL,
	tst_metadata_generate,
	NULL
};

/*
 * Opcode definitions
 */

static bool tst_metadata_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_metadata_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Metadata operation */

const struct sieve_operation_def metadata_operation = {
	"METADATA",
	&mboxmetadata_extension,
	EXT_METADATA_OPERATION_METADATA,
	tst_metadata_operation_dump,
	tst_metadata_operation_execute
};

/* Servermetadata operation */

const struct sieve_operation_def servermetadata_operation = {
	"SERVERMETADATA",
	&servermetadata_extension,
	EXT_METADATA_OPERATION_METADATA,
	tst_metadata_operation_dump,
	tst_metadata_operation_execute
};

/*
 * Test registration
 */

static bool tst_metadata_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_command_registration *cmd_reg)
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(valdtr, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);

	return TRUE;
}

/*
 * Test validation
 */

static bool tst_metadata_validate
(struct sieve_validator *valdtr, struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	const struct sieve_match_type mcht_default =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	const struct sieve_comparator cmp_default =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	unsigned int arg_index = 1;

	if ( sieve_command_is(tst, metadata_test) ) {
		if ( !sieve_validate_positional_argument
			(valdtr, tst, arg, "mailbox", arg_index++, SAAT_STRING) ) {
			return FALSE;
		}

		if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
			return FALSE;

		arg = sieve_ast_argument_next(arg);
	}

	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "annotation-name", arg_index++, SAAT_STRING) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	arg = sieve_ast_argument_next(arg);

	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "key list", arg_index++, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(valdtr, tst, arg, &mcht_default, &cmp_default);
}

/*
 * Test generation
 */

static bool tst_metadata_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *tst)
{
	if ( sieve_command_is(tst, metadata_test) ) {
		sieve_operation_emit
			(cgenv->sblock, tst->ext, &metadata_operation);
	} else if ( sieve_command_is(tst, servermetadata_test) ) {
		sieve_operation_emit
			(cgenv->sblock, tst->ext, &servermetadata_operation);
	} else {
		i_unreached();
	}

 	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, tst, NULL) )
		return FALSE;

	return TRUE;
}

/*
 * Code dump
 */

static bool tst_metadata_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	bool metadata = sieve_operation_is(denv->oprtn, metadata_operation);

	if ( metadata )
		sieve_code_dumpf(denv, "METADATA");
	else
		sieve_code_dumpf(denv, "SERVERMETADATA");

	sieve_code_descend(denv);

	/* Handle any optional arguments */
	if ( sieve_match_opr_optional_dump(denv, address, NULL) != 0 )
 		return FALSE;

	if ( metadata && !sieve_opr_string_dump(denv, address, "mailbox") )
		return FALSE;

	return
		sieve_opr_string_dump(denv, address, "annotation-name") &&
		sieve_opr_stringlist_dump(denv, address, "key list");
}

/*
 * Code execution
 */

static int tst_metadata_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool metadata = sieve_operation_is(renv->oprtn, metadata_operation);
	struct sieve_match_type mcht =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	struct sieve_comparator cmp =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	string_t *mailbox, *annotation_name;
	struct sieve_stringlist *value_list, *key_list;
	const char *annotation = NULL;
	int match, ret;

	/*
	 * Read operands
	 */

	/* Handle match-type and comparator operands */
	if ( sieve_match_opr_optional_read
		(renv, address, NULL, &ret, &cmp, &mcht) < 0 )
		return ret;

	/* Read mailbox */
	if ( metadata ) {
		if ( (ret=sieve_opr_string_read(renv, address, "mailbox", &mailbox)) <= 0 )
			return ret;
	}

	/* Read annotation-name */
	if ( (ret=sieve_opr_string_read
		(renv, address, "annotation-name", &annotation_name)) <= 0 )
		return ret;

	/* Read key-list */
	if ( (ret=sieve_opr_stringlist_read
		(renv, address, "key-list", &key_list)) <= 0 )
		return ret;

	/*
	 * Perform operation
	 */

	if ( metadata )
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "metadata test");
	else
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "servermetadata test");

	/* Get annotation */
	annotation = "FIXME";

	/* Perform match */
	if ( annotation != NULL ) {
		/* Create value stringlist */
		value_list = sieve_single_stringlist_create_cstr(renv, annotation, FALSE);

		/* Perform match */
		if ( (match=sieve_match(renv, &mcht, &cmp, value_list, key_list, &ret))
			< 0 )
			return ret;
	} else {
		match = 0;
	}

	/* Set test result for subsequent conditional jump */
	sieve_interpreter_set_test_result(renv->interp, match > 0);
	return SIEVE_EXEC_OK;
}
