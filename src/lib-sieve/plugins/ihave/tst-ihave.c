/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-ihave-common.h"

/*
 * Ihave test
 *
 * Syntax:
 *   ihave <capabilities: string-list>
 */

static bool tst_ihave_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_ihave_validate_const
	(struct sieve_validator *valdtr, struct sieve_command *tst,
		int *const_current, int const_next);
static bool tst_ihave_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *tst);

const struct sieve_command_def ihave_test = {
	.identifier = "ihave",
	.type = SCT_TEST,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = tst_ihave_validate,
	.validate_const = tst_ihave_validate_const,
	.generate = tst_ihave_generate
};

/*
 * Ihave operation
 */

static bool tst_ihave_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_ihave_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def tst_ihave_operation = {
	.mnemonic = "IHAVE",
	.ext_def = &ihave_extension,
	.code = EXT_IHAVE_OPERATION_IHAVE,
	.dump = tst_ihave_operation_dump,
	.execute = tst_ihave_operation_execute
};

/*
 * Code validation
 */

static bool tst_ihave_validate
(struct sieve_validator *valdtr, struct sieve_command *tst)
{
	struct _capability {
		const struct sieve_extension *ext;
		struct sieve_ast_argument *arg;
	};

	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_ast_argument *stritem;
	enum sieve_compile_flags cpflags = sieve_validator_compile_flags(valdtr);
	bool no_global = ( (cpflags & SIEVE_COMPILE_FLAG_NOGLOBAL) != 0 );
	ARRAY(struct _capability) capabilities;
	struct _capability capability;
	const struct _capability *caps;
	unsigned int i, count;
	bool all_known = TRUE;

	t_array_init(&capabilities, 64);

	tst->data = (void *) FALSE;

	/* Check stringlist argument */
	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "capabilities", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	switch ( sieve_ast_argument_type(arg) ) {
	case SAAT_STRING:
		/* Single string */
		capability.arg = arg;
		capability.ext = sieve_extension_get_by_name
			(tst->ext->svinst, sieve_ast_argument_strc(arg));

		if ( capability.ext == NULL || (no_global && capability.ext->global)) {
			all_known = FALSE;

			ext_ihave_ast_add_missing_extension
				(tst->ext, tst->ast_node->ast, sieve_ast_argument_strc(arg));
		} else {
			array_append(&capabilities, &capability, 1);
		}

		break;

	case SAAT_STRING_LIST:
		/* String list */
		stritem = sieve_ast_strlist_first(arg);

		while ( stritem != NULL ) {
			capability.arg = stritem;
			capability.ext = sieve_extension_get_by_name
				(tst->ext->svinst, sieve_ast_argument_strc(stritem));

			if ( capability.ext == NULL || (no_global && capability.ext->global)) {
				all_known = FALSE;

				ext_ihave_ast_add_missing_extension
					(tst->ext, tst->ast_node->ast, sieve_ast_argument_strc(stritem));
			} else {
				array_append(&capabilities, &capability, 1);
			}

			stritem = sieve_ast_strlist_next(stritem);
		}

		break;
	default:
		i_unreached();
	}

	if ( !all_known )
		return TRUE;

	/* RFC 5463, Section 4, page 4:
	 *
	 * The "ihave" extension is designed to be used with other extensions
	 * that add tests, actions, comparators, or arguments.  Implementations
	 * MUST NOT allow it to be used with extensions that change the
	 * underlying Sieve grammar, or extensions like encoded-character
	 * [RFC5228], or variables [RFC5229] that change how the content of
	 * Sieve scripts are interpreted.  The test MUST fail and the extension
	 * MUST NOT be enabled if such usage is attempted.
	 *
	 * FIXME: current implementation of this restriction is hardcoded and
	 * therefore highly inflexible
	 */
	caps = array_get(&capabilities, &count);
	for ( i = 0; i < count; i++ ) {
		if ( sieve_extension_name_is(caps[i].ext, "variables") ||
			sieve_extension_name_is(caps[i].ext, "encoded-character") )
			return TRUE;
	}

	/* Load all extensions */
	caps = array_get(&capabilities, &count);
	for ( i = 0; i < count; i++ ) {
		if ( !sieve_validator_extension_load
			(valdtr, tst, caps[i].arg, caps[i].ext, FALSE) )
			return FALSE;
	}

	if ( !sieve_validator_argument_activate
		(valdtr, tst, arg, FALSE) )
		return FALSE;

	tst->data = (void *) TRUE;
	return TRUE;
}

static bool tst_ihave_validate_const
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_command *tst,
	int *const_current, int const_next ATTR_UNUSED)
{
	if ( (bool)tst->data == TRUE )
		*const_current = -1;
	else
		*const_current = 0;
	return TRUE;
}

/*
 * Code generation
 */

bool tst_ihave_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *tst)
{
	/* Emit opcode */
	sieve_operation_emit(cgenv->sblock,
		tst->ext, &tst_ihave_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

/*
 * Code dump
 */

static bool tst_ihave_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "IHAVE");
	sieve_code_descend(denv);

	return sieve_opr_stringlist_dump
		(denv, address, "capabilities");
}

/*
 * Code execution
 */

static int tst_ihave_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_instance *svinst = renv->svinst;
	struct sieve_stringlist *capabilities;
	string_t *cap_item;
	bool matched;
	int ret;

	/*
	 * Read operands
	 */

	/* Read capabilities */
	if ( (ret=sieve_opr_stringlist_read
		(renv, address, "capabilities", &capabilities)) <= 0 )
		return ret;

	/*
	 * Perform test
	 */

	/* Perform the test */
	sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "ihave test");
	sieve_runtime_trace_descend(renv);

	cap_item = NULL;
	matched = TRUE;
	while ( matched &&
		(ret=sieve_stringlist_next_item(capabilities, &cap_item)) > 0 ) {
		const struct sieve_extension *ext;
		int sret;

		ext = sieve_extension_get_by_name(svinst, str_c(cap_item));
		if (ext == NULL) {
			sieve_runtime_trace_error(renv,
				"ihave: invalid extension name");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
		sret = sieve_interpreter_extension_start(renv->interp, ext);
		if ( sret == SIEVE_EXEC_FAILURE ) {
			sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
				"extension `%s' not available",
				sieve_extension_name(ext));
			matched = FALSE;
		} else if ( sret == SIEVE_EXEC_OK ) {
			sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
				"extension `%s' available",
				sieve_extension_name(ext));
		} else {
			return sret;
		}
	}
	if ( ret < 0 ) {
		sieve_runtime_trace_error(renv,
			"invalid capabilities item");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Set test result for subsequent conditional jump */
	sieve_interpreter_set_test_result(renv->interp, matched);
	return SIEVE_EXEC_OK;
}
