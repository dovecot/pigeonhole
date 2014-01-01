/* Copyright (c) 2002-2014 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"
#include "mail-storage.h"
#include "mail-namespace.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-metadata-common.h"

/*
 * Command definitions
 */

/* Forward declarations */

static bool tst_metadataexists_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_metadataexists_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

/* Metadataexists command
 *
 * Syntax:
 *    metadataexists <mailbox: string> <annotation-names: string-list>
 */

static bool tst_metadataexists_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_metadataexists_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

const struct sieve_command_def metadataexists_test = {
	"metadataexists",
	SCT_TEST,
	2, 0, FALSE, FALSE,
	NULL, NULL,
	tst_metadataexists_validate,
	NULL,
	tst_metadataexists_generate,
	NULL
};

/* Servermetadataexists command
 *
 * Syntax:
 *    servermetadataexists <annotation-names: string-list>
 */

const struct sieve_command_def servermetadataexists_test = {
	"servermetadataexists",
	SCT_TEST,
	1, 0, FALSE, FALSE,
	NULL, NULL,
	tst_metadataexists_validate,
	NULL,
	tst_metadataexists_generate,
	NULL
};


/*
 * Opcode definitions
 */

static bool tst_metadataexists_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_metadataexists_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Metadata operation */

const struct sieve_operation_def metadataexists_operation = {
	"METADATAEXISTS",
	&mboxmetadata_extension,
	EXT_METADATA_OPERATION_METADATAEXISTS,
	tst_metadataexists_operation_dump,
	tst_metadataexists_operation_execute
};

/* Mailboxexists operation */

const struct sieve_operation_def servermetadataexists_operation = {
	"SERVERMETADATAEXISTS",
	&servermetadata_extension,
	EXT_METADATA_OPERATION_METADATAEXISTS,
	tst_metadataexists_operation_dump,
	tst_metadataexists_operation_execute
};

/*
 * Test validation
 */

static bool tst_metadataexists_validate
(struct sieve_validator *valdtr, struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	unsigned int arg_index = 1;

	if ( sieve_command_is(tst, metadataexists_test) ) {
		if ( !sieve_validate_positional_argument
			(valdtr, tst, arg, "mailbox", arg_index++, SAAT_STRING) ) {
			return FALSE;
		}

		if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
			return FALSE;

		arg = sieve_ast_argument_next(arg);
	}

	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "annotation-names", arg_index++, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	return sieve_validator_argument_activate(valdtr, tst, arg, FALSE);
}

/*
 * Test generation
 */

static bool tst_metadataexists_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *tst)
{
	if ( sieve_command_is(tst, metadataexists_test) ) {
		sieve_operation_emit
			(cgenv->sblock, tst->ext, &metadataexists_operation);
	} else if ( sieve_command_is(tst, servermetadataexists_test) ) {
		sieve_operation_emit
			(cgenv->sblock, tst->ext, &servermetadataexists_operation);
	} else {
		i_unreached();
	}

 	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

/*
 * Code dump
 */

static bool tst_metadataexists_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	bool metadata = sieve_operation_is(denv->oprtn, metadataexists_operation);

	if ( metadata )
		sieve_code_dumpf(denv, "METADATAEXISTS");
	else
		sieve_code_dumpf(denv, "SERVERMETADATAEXISTS");

	sieve_code_descend(denv);

	if ( metadata && !sieve_opr_string_dump(denv, address, "mailbox") )
		return FALSE;

	return
		sieve_opr_stringlist_dump(denv, address, "annotation-names");
}

/*
 * Code execution
 */

static int tst_metadataexists_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool metadata = sieve_operation_is(renv->oprtn, metadataexists_operation);
	struct sieve_stringlist *annotation_names;
	string_t *mailbox, *annotation_item;
	bool trace = FALSE;
	bool all_exist = TRUE;
	int ret;

	/*
	 * Read operands
	 */

	/* Read mailbox */
	if ( metadata ) {
		if ( (ret=sieve_opr_string_read(renv, address, "mailbox", &mailbox)) <= 0 )
			return ret;
	}

	/* Read annotation names */
	if ( (ret=sieve_opr_stringlist_read
		(renv, address, "annotation-names", &annotation_names)) <= 0 )
		return ret;

	/*
	 * Perform operation
	 */

	if ( sieve_runtime_trace_active(renv, SIEVE_TRLVL_TESTS) ) {
		if ( metadata )
			sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "metadataexists test");
		else
			sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "servermetadataexists test");

		sieve_runtime_trace_descend(renv);

		trace = sieve_runtime_trace_active(renv, SIEVE_TRLVL_MATCHING);
	}

	if ( renv->scriptenv->user != NULL ) {
		int ret;

		annotation_item = NULL;
		while ( (ret=sieve_stringlist_next_item(annotation_names, &annotation_item))
			> 0 ) {
			//const char *annotation = str_c(annotation_item);

			/* IMPLEMENT ... */
			all_exist = FALSE;
		}

		if ( ret < 0 ) {
			sieve_runtime_trace_error
				(renv, "invalid annotation name stringlist item");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	}

	if ( trace ) {
		if ( all_exist )
			sieve_runtime_trace(renv, 0, "all annotations exist");
		else
			sieve_runtime_trace(renv, 0, "some mailboxes no not exist");
	}

	sieve_interpreter_set_test_result(renv->interp, all_exist);
	return SIEVE_EXEC_OK;
}
