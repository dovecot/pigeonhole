/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"
#include "mail-storage.h"

#include "rfc2822.h"
#include "edit-mail.h"

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

#include "ext-editheader-common.h"

/*
 * Deleteheader command
 *
 * Syntax:
 *   deleteheader [":index" <fieldno: number> [":last"]]
 *                [COMPARATOR] [MATCH-TYPE]
 *                <field-name: string> [<value-patterns: string-list>]
 */

static bool cmd_deleteheader_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool cmd_deleteheader_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool cmd_deleteheader_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd);

const struct sieve_command_def deleteheader_command = {
	"deleteheader",
	SCT_COMMAND,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE,
	cmd_deleteheader_registered,
	NULL,
	cmd_deleteheader_validate,
	NULL,
	cmd_deleteheader_generate,
	NULL
};

/*
 * Deleteheader command tags
 */

/* Forward declarations */

static bool cmd_deleteheader_validate_index_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);
static bool cmd_deleteheader_validate_last_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);

/* Argument objects */

static const struct sieve_argument_def deleteheader_index_tag = {
	"index",
	NULL,
	cmd_deleteheader_validate_index_tag,
	NULL, NULL, NULL
};

static const struct sieve_argument_def deleteheader_last_tag = {
	"last",
	NULL,
	cmd_deleteheader_validate_last_tag,
	NULL, NULL, NULL
};

/* Codes for optional arguments */

enum cmd_deleteheader_optional {
	OPT_INDEX = SIEVE_MATCH_OPT_LAST,
	OPT_LAST
};

/*
 * Deleteheader operation
 */

static bool cmd_deleteheader_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_deleteheader_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def deleteheader_operation = {
	"DELETEHEADER",
	&editheader_extension,
	EXT_EDITHEADER_OPERATION_DELETEHEADER,
	cmd_deleteheader_operation_dump,
	cmd_deleteheader_operation_execute
};

/*
 * Command registration
 */

static bool cmd_deleteheader_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_command_registration *cmd_reg)
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(valdtr, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);

	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &deleteheader_index_tag, OPT_INDEX);
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &deleteheader_last_tag, OPT_LAST);

	return TRUE;
}

/*
 * Command validation context
 */

struct cmd_deleteheader_context_data {
	struct sieve_ast_argument *arg_index;
	struct sieve_ast_argument *arg_last;
};

/*
 * Tag validation
 */

static struct cmd_deleteheader_context_data *
cmd_deleteheader_get_context
(struct sieve_command *cmd)
{
	struct cmd_deleteheader_context_data *ctx_data =
		(struct cmd_deleteheader_context_data *)cmd->data;

	if ( ctx_data != NULL ) return ctx_data;

	ctx_data = p_new
		(sieve_command_pool(cmd), struct cmd_deleteheader_context_data, 1);
	cmd->data = (void *)ctx_data;

	return ctx_data;
}

static bool cmd_deleteheader_validate_index_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
	struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct cmd_deleteheader_context_data *ctx_data;
	sieve_number_t index;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Check syntax:
	 *   :index number
	 */
	if ( !sieve_validate_tag_parameter
		(valdtr, cmd, tag, *arg, NULL, 0, SAAT_NUMBER, FALSE) ) {
		return FALSE;
	}

	index = sieve_ast_argument_number(*arg);
	if ( index > INT_MAX ) {
		sieve_argument_validate_warning(valdtr, *arg,
			"the :%s tag for the %s %s has a parameter value '%lu' "
			"exceeding the maximum (%d)",
			sieve_argument_identifier(tag), sieve_command_identifier(cmd),
			sieve_command_type_name(cmd), (unsigned long) index, INT_MAX);
		return FALSE;
	}

	ctx_data = cmd_deleteheader_get_context(cmd);
	ctx_data->arg_index = *arg;

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool cmd_deleteheader_validate_last_tag
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_ast_argument **arg,
	struct sieve_command *cmd)
{
	struct cmd_deleteheader_context_data *ctx_data;

	ctx_data = cmd_deleteheader_get_context(cmd);
	ctx_data->arg_last = *arg;

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/*
 * Validation
 */

static bool cmd_deleteheader_validate
(struct sieve_validator *valdtr, struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct cmd_deleteheader_context_data *ctx_data =
		(struct cmd_deleteheader_context_data *)cmd->data;
	struct sieve_comparator cmp_default =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	struct sieve_match_type mcht_default =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);

	if ( ctx_data != NULL ) {
		if ( ctx_data->arg_last != NULL && ctx_data->arg_index == NULL ) {
			sieve_argument_validate_error(valdtr, ctx_data->arg_last,
				"the :last tag for the %s %s cannot be specified "
				"without the :index tag",
				sieve_command_identifier(cmd), sieve_command_type_name(cmd));
		}
	}

	/* Field name argument */

	if ( arg == NULL ) {
		sieve_command_validate_error(valdtr, cmd,
			"the %s %s expects at least one positional argument, but none was found",
			sieve_command_identifier(cmd), sieve_command_type_name(cmd));
		return FALSE;
	}

	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "field name", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, cmd, arg, FALSE) )
		return FALSE;

	if ( sieve_argument_is_string_literal(arg) ) {
		string_t *fname = sieve_ast_argument_str(arg);

		if ( !rfc2822_header_field_name_verify(str_c(fname), str_len(fname)) ) {
			sieve_argument_validate_error(valdtr, arg, "deleteheader command:"
				"specified field name `%s' is invalid",
				str_sanitize(str_c(fname), 80));
			return FALSE;
		}

		if ( ext_editheader_header_is_protected(cmd->ext, str_c(fname)) ) {
			sieve_argument_validate_warning(valdtr, arg, "deleteheader command: "
				"specified header field `%s' is protected; "
				"modification will be denied", str_sanitize(str_c(fname), 80));
		}
	}

	/* Value patterns argument */

	arg = sieve_ast_argument_next(arg);
	if ( arg == NULL ) {
		/* There is none; let's not generate code for useless match arguments */
		sieve_match_type_arguments_remove(valdtr, cmd);

		return TRUE;
	}

	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "value patterns", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, cmd, arg, FALSE) )
		return FALSE;

	/* Validate the value patterns to a specified match type */
	return sieve_match_type_validate
		(valdtr, cmd, arg, &mcht_default, &cmp_default);
}

/*
 * Code generation
 */

static bool cmd_deleteheader_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &deleteheader_operation);

 	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;

	/* Emit a placeholder when the value-patterns argument is missing */
	if ( sieve_ast_argument_next(cmd->first_positional) == NULL )
		sieve_opr_omitted_emit(cgenv->sblock);

	return TRUE;
}

/*
 * Code dump
 */

static bool cmd_deleteheader_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "DELETEHEADER");
	sieve_code_descend(denv);

	/* Optional operands */
	for (;;) {
		int opt;

		if ( (opt=sieve_match_opr_optional_dump(denv, address, &opt_code)) < 0 )
			return FALSE;

		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case OPT_INDEX:
			if ( !sieve_opr_number_dump(denv, address, "index") )
				return FALSE;
			break;
		case OPT_LAST:
			sieve_code_dumpf(denv, "last");
			break;
		default:
			return FALSE;
		}
	};

	if ( !sieve_opr_string_dump(denv, address, "field name") )
		return FALSE;

	return sieve_opr_stringlist_dump_ex(denv, address, "value patterns", "");
}

/*
 * Code execution
 */

static int cmd_deleteheader_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	int opt_code = 0;
	struct sieve_comparator cmp =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	struct sieve_match_type mcht =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	string_t *field_name;
	struct sieve_stringlist *vpattern_list = NULL;
	struct edit_mail *edmail;
	sieve_number_t index_offset = 0;
	bool index_last = FALSE;
	bool trace = FALSE;
	int ret;

	/*
	 * Read operands
	 */

	for (;;) {
		int opt;

		if ( (opt=sieve_match_opr_optional_read
			(renv, address, &opt_code, &ret, &cmp, &mcht)) < 0 )
			return ret;

		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case OPT_INDEX:
			if ( (ret=sieve_opr_number_read(renv, address, "index", &index_offset))
				<= 0 )
				return ret;

			if ( index_offset > INT_MAX ) {
				sieve_runtime_trace_error(renv, "index is > %d", INT_MAX);
				return SIEVE_EXEC_BIN_CORRUPT;
			}

			break;
		case OPT_LAST:
			index_last = TRUE;
			break;
		default:
			sieve_runtime_trace_error(renv, "unknown optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	}

	/* Read field-name */
	if ( (ret=sieve_opr_string_read(renv, address, "field-name", &field_name))
		<= 0 )
		return ret;

	/* Read value-patterns */
	if ( (ret=sieve_opr_stringlist_read_ex
		(renv, address, "value-patterns", TRUE, &vpattern_list)) <= 0 )
		return ret;

	/*
	 * Verify arguments
	 */

	if ( !rfc2822_header_field_name_verify
		(str_c(field_name), str_len(field_name)) ) {
		sieve_runtime_error(renv, NULL, "deleteheader action: "
			"specified field name `%s' is invalid",
			str_sanitize(str_c(field_name), 80));
		return SIEVE_EXEC_FAILURE;
	}

	if ( ext_editheader_header_is_protected(this_ext, str_c(field_name)) ) {
		sieve_runtime_warning(renv, NULL, "deleteheader action: "
			"specified header field `%s' is protected; modification denied",
			str_sanitize(str_c(field_name), 80));
		return SIEVE_EXEC_OK;
	}

	/*
	 * Execute command
	 */

	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS, "deleteheader command");

	/* Start editing the mail */
	edmail = sieve_message_edit(renv->msgctx);

	trace = sieve_runtime_trace_active(renv, SIEVE_TRLVL_COMMANDS);

	/* Either do string matching or just kill all/indexed notify action(s) */
	if ( vpattern_list != NULL ) {
		struct edit_mail_header_iter *edhiter;
		struct sieve_match_context *mctx;

		if ( trace ) {
			sieve_runtime_trace_descend(renv);
			if ( index_offset != 0 ) {
				sieve_runtime_trace(renv, 0,
					"deleting matching occurences of header `%s' at index %u%s",
					str_c(field_name), index_offset, ( index_last ? " from last": ""));
			} else {
				sieve_runtime_trace(renv, 0,
					"deleting matching occurences of header `%s'", str_c(field_name));
			}
		}

		/* Iterate through all headers and delete those that match */
		if ( (ret=edit_mail_headers_iterate_init
			(edmail, str_c(field_name), index_last, &edhiter)) > 0 )
		{
			int mret = 0;
			sieve_number_t pos = 0;

			/* Initialize match */
			mctx = sieve_match_begin(renv, &mcht, &cmp);

			/* Match */
			do {
				pos++;

				/* Check index if any */
				if ( index_offset == 0 || pos == index_offset ) {
					const char *value;
					int match;

					/* Match value against all value patterns */
					edit_mail_headers_iterate_get(edhiter, &value);
					if ( (match=sieve_match_value
						(mctx, value, strlen(value), vpattern_list)) < 0 )
						break;

					if ( match > 0 ) {
						/* Remove it and iterate to next */
						sieve_runtime_trace(renv, 0, "deleting header with value `%s'",
							value);

						if ( !edit_mail_headers_iterate_remove(edhiter) ) break;
						continue;
					}
				}

			} while ( edit_mail_headers_iterate_next(edhiter) );

			/* Finish match */
			mret = sieve_match_end(&mctx, &ret);

			edit_mail_headers_iterate_deinit(&edhiter);

			if ( mret < 0 )
				return ret;
		}

		if ( ret == 0 ) {
			sieve_runtime_trace(renv, 0, "header `%s' not found", str_c(field_name));
		} else if ( ret < 0 ) {
			sieve_runtime_warning(renv, NULL, "deleteheader action: "
				"failed to delete occurences of header `%s' (this should not happen!)",
				str_c(field_name));
		}

	} else {
		int index = ( index_last ? -((int)index_offset) : ((int)index_offset) );

		if ( trace ) {
			sieve_runtime_trace_descend(renv);
			if ( index_offset != 0 ) {
				sieve_runtime_trace(renv, 0, "deleting header `%s' at index %u%s",
					str_c(field_name), index_offset, ( index_last ? " from last": ""));
			} else {
				sieve_runtime_trace(renv, 0, "deleting header `%s'", str_c(field_name));
			}
		}

		/* Delete all occurences of header */
		ret = edit_mail_header_delete(edmail, str_c(field_name), index);

		if ( ret < 0 ) {
			sieve_runtime_warning(renv, NULL, "deleteheader action: "
				"failed to delete occurences of header `%s' (this should not happen!)",
				str_c(field_name));
		} else if ( trace ) {
			sieve_runtime_trace(renv, 0, "deleted %d occurences of header `%s'",
				ret, str_c(field_name));
		}

	}

	return SIEVE_EXEC_OK;
}
