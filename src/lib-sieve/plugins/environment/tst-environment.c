/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
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

#include "ext-environment-common.h"

/*
 * Environment test
 *
 * Syntax:
 *   environment [COMPARATOR] [MATCH-TYPE]
 *      <name: string> <key-list: string-list>
 */

static bool
tst_environment_registered(struct sieve_validator *valdtr,
			   const struct sieve_extension *ext,
			   struct sieve_command_registration *cmd_reg);
static bool
tst_environment_validate(struct sieve_validator *valdtr,
			 struct sieve_command *tst);
static bool
tst_environment_generate(const struct sieve_codegen_env *cgenv,
			 struct sieve_command *cmd);

const struct sieve_command_def tst_environment = {
	.identifier = "environment",
	.type = SCT_TEST,
	.positional_args = 2,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = tst_environment_registered,
	.validate = tst_environment_validate,
	.generate = tst_environment_generate,
};

/*
 * Environment operation
 */

static bool
tst_environment_operation_dump(const struct sieve_dumptime_env *denv,
			       sieve_size_t *address);
static int
tst_environment_operation_execute(const struct sieve_runtime_env *renv,
				  sieve_size_t *address);

const struct sieve_operation_def tst_environment_operation = {
	.mnemonic = "ENVIRONMENT",
	.ext_def = &environment_extension,
	.dump = tst_environment_operation_dump,
	.execute = tst_environment_operation_execute,
};

/*
 * Test registration
 */

static bool
tst_environment_registered(struct sieve_validator *valdtr,
			   const struct sieve_extension *ext ATTR_UNUSED,
			   struct sieve_command_registration *cmd_reg)
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(valdtr, cmd_reg,
				   SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg,
				    SIEVE_MATCH_OPT_MATCH_TYPE);

	return TRUE;
}

/*
 * Test validation
 */

static bool
tst_environment_validate(struct sieve_validator *valdtr,
			 struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	const struct sieve_match_type mcht_default =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	const struct sieve_comparator cmp_default =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);

	if (!sieve_validate_positional_argument(valdtr, tst, arg,
						"name", 1, SAAT_STRING))
		return FALSE;
	if (!sieve_validator_argument_activate(valdtr, tst, arg, FALSE))
		return FALSE;

	arg = sieve_ast_argument_next(arg);

	if (!sieve_validate_positional_argument(valdtr, tst, arg, "key list", 2,
						SAAT_STRING_LIST))
		return FALSE;
	if (!sieve_validator_argument_activate(valdtr, tst, arg, FALSE))
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate(valdtr, tst, arg,
					 &mcht_default, &cmp_default);
}

/*
 * Test generation
 */

static bool
tst_environment_generate(const struct sieve_codegen_env *cgenv,
			 struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext,
			     &tst_environment_operation);

 	/* Generate arguments */
	return sieve_generate_arguments(cgenv, cmd, NULL);
}

/*
 * Code dump
 */

static bool
tst_environment_operation_dump(const struct sieve_dumptime_env *denv,
			       sieve_size_t *address)
{
	sieve_code_dumpf(denv, "ENVIRONMENT");
	sieve_code_descend(denv);

	/* Optional operands */
	if (sieve_match_opr_optional_dump(denv, address, NULL) != 0)
		return FALSE;

	return (sieve_opr_string_dump(denv, address, "name") &&
		sieve_opr_stringlist_dump(denv, address, "key list"));
}

/*
 * Code execution
 */

static int
tst_environment_operation_execute(const struct sieve_runtime_env *renv,
				  sieve_size_t *address)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct sieve_match_type mcht =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	struct sieve_comparator cmp =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	string_t *name;
	struct sieve_stringlist *value_list, *key_list;
	const char *env_item;
	int match, ret;

	/*
	 * Read operands
	 */

	/* Handle match-type and comparator operands */
	if (sieve_match_opr_optional_read(renv, address, NULL,
					  &ret, &cmp, &mcht) < 0)
		return ret;

	/* Read source */
	ret = sieve_opr_string_read(renv, address, "name", &name);
	if (ret <= 0)
		return ret;

	/* Read key-list */
	ret = sieve_opr_stringlist_read(renv, address, "key-list", &key_list);
	if (ret <= 0)
		return ret;

	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "environment test");

	env_item = ext_environment_item_get_value(this_ext, renv, str_c(name));

	if (env_item != NULL) {
		/* Construct value list */
		value_list = sieve_single_stringlist_create_cstr(
			renv, env_item, FALSE);

		/* Perform match */
		match = sieve_match(renv, &mcht, &cmp, value_list, key_list,
				    &ret);
		if (ret < 0)
			return ret;
	} else {
		match = 0;

		sieve_runtime_trace_descend(renv);
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
				    "environment item `%s' not found",
				    str_sanitize(str_c(name), 128));
	}

	/* Set test result for subsequent conditional jump */
	sieve_interpreter_set_test_result(renv->interp, match > 0);
	return SIEVE_EXEC_OK;
}
