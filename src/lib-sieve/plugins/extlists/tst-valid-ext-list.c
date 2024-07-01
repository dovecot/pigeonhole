/* Copyright (c) 2025 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"

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

#include "ext-extlists-common.h"

/*
   Valid_ext_list test

   Syntax:
     valid_ext_list <ext-list-names: string-list>
 */

static bool
tst_vextlist_validate(struct sieve_validator *valdtr,
		      struct sieve_command *tst);
static bool
tst_vextlist_generate(const struct sieve_codegen_env *cgenv,
		      struct sieve_command *ctx);

const struct sieve_command_def valid_ext_list_test = {
	.identifier = "valid_ext_list",
	.type = SCT_TEST,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = tst_vextlist_validate,
	.generate = tst_vextlist_generate
};

/*
 * Valid_ext_list operation
 */

static bool
tst_vextlist_operation_dump(const struct sieve_dumptime_env *denv,
			    sieve_size_t *address);
static int
tst_vextlist_operation_execute(const struct sieve_runtime_env *renv,
			       sieve_size_t *address);

const struct sieve_operation_def valid_ext_list_operation = {
	.mnemonic = "VALID_EXT_LIST",
	.ext_def = &extlists_extension,
	.dump = tst_vextlist_operation_dump,
	.execute = tst_vextlist_operation_execute
};

/*
 * Test validation
 */

static bool
tst_vextlist_validate(struct sieve_validator *valdtr, struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;

	if (!sieve_validate_positional_argument(valdtr, tst, arg,
						"ext-list-names", 1,
						SAAT_STRING_LIST))
		return FALSE;

	return sieve_validator_argument_activate(valdtr, tst, arg, FALSE);
}

/*
 * Test generation
 */

static bool
tst_vextlist_generate(const struct sieve_codegen_env *cgenv,
		      struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &valid_ext_list_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, cmd, NULL);
}

/*
 * Code dump
 */

static bool
tst_vextlist_operation_dump(const struct sieve_dumptime_env *denv,
			    sieve_size_t *address)
{
	sieve_code_dumpf(denv, "VALID_EXT_LIST");
	sieve_code_descend(denv);

	return sieve_opr_stringlist_dump(denv, address, "ext-list-names");
}

/*
 * Code execution
 */

static int
tst_vextlist_operation_execute(const struct sieve_runtime_env *renv,
			       sieve_size_t *address)
{
	struct sieve_stringlist *ext_list_names;
	string_t *list_name_item;
	bool all_valid = TRUE, warned = FALSE;
	int ret;

	/*
	 * Read operands
	 */

	/* Read list names */
	ret = sieve_opr_stringlist_read(renv, address, "ext-list-names",
					&ext_list_names);
	if (ret <= 0)
		return ret;

	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "valid_ext_list_test");

	list_name_item = NULL;
	while ((ret = sieve_stringlist_next_item(ext_list_names,
						 &list_name_item)) > 0) {
		int vret;

		vret = ext_extlists_runtime_ext_list_validate(
			renv, list_name_item);
		if (vret < 0) {
			all_valid = FALSE;

			if (warned)
				break;
			warned = TRUE;

			sieve_runtime_warning(
				renv, NULL, ":list tag: "
				"invalid external list name: %s",
				str_sanitize_utf8(str_c(list_name_item), 1024));
			break;
		}
		if (vret == 0) {
			all_valid = FALSE;
			break;
		}
	}

	if (ret < 0) {
		sieve_runtime_trace_error(renv,
			"corrupt external list name item");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	sieve_interpreter_set_test_result(renv->interp, all_valid);
	return SIEVE_EXEC_OK;
}
