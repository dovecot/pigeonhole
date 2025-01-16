/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-ast.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-binary.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-include-common.h"
#include "ext-include-binary.h"

/*
 * Include command
 *
 * Syntax:
 *   include [LOCATION] [":once"] [":optional"] <value: string>
 *
 * [LOCATION]:
 *   ":personal" / ":global"
 */

static bool
cmd_include_registered(struct sieve_validator *valdtr,
		       const struct sieve_extension *ext,
		       struct sieve_command_registration *cmd_reg);
static bool
cmd_include_pre_validate(struct sieve_validator *valdtr ATTR_UNUSED,
			 struct sieve_command *cmd);
static bool
cmd_include_validate(struct sieve_validator *valdtr,
		     struct sieve_command *cmd);
static bool
cmd_include_generate(const struct sieve_codegen_env *cgenv,
		     struct sieve_command *ctx);

const struct sieve_command_def cmd_include = {
	.identifier = "include",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = cmd_include_registered,
	.pre_validate = cmd_include_pre_validate,
	.validate = cmd_include_validate,
	.generate = cmd_include_generate,
};

/*
 * Include operation
 */

static bool
opc_include_dump(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int
opc_include_execute(const struct sieve_runtime_env *renv,
		    sieve_size_t *address);

const struct sieve_operation_def include_operation = {
	.mnemonic = "include",
	.ext_def = &include_extension,
	.code = EXT_INCLUDE_OPERATION_INCLUDE,
	.dump = opc_include_dump,
	.execute = opc_include_execute,
};

/*
 * Context structures
 */

struct cmd_include_context_data {
	enum ext_include_script_location location;
	const char *script_name;
	struct sieve_script *script;
	enum ext_include_flags flags;

	bool location_assigned:1;
};

/*
 * Tagged arguments
 */

static bool
cmd_include_validate_location_tag(struct sieve_validator *valdtr,
				  struct sieve_ast_argument **arg,
				  struct sieve_command *cmd);

static const struct sieve_argument_def include_personal_tag = {
	.identifier = "personal",
	.validate = cmd_include_validate_location_tag,
};

static const struct sieve_argument_def include_global_tag = {
	.identifier = "global",
	.validate = cmd_include_validate_location_tag,
};

static bool
cmd_include_validate_boolean_tag(struct sieve_validator *valdtr,
				 struct sieve_ast_argument **arg,
				 struct sieve_command *cmd);

static const struct sieve_argument_def include_once_tag = {
	.identifier = "once",
	.validate = cmd_include_validate_boolean_tag,
};

static const struct sieve_argument_def include_optional_tag = {
	.identifier = "optional",
	.validate = cmd_include_validate_boolean_tag,
};

/*
 * Tag validation
 */

static bool
cmd_include_validate_location_tag(struct sieve_validator *valdtr,
				  struct sieve_ast_argument **arg,
				  struct sieve_command *cmd)
{
	struct cmd_include_context_data *ctx_data =
		(struct cmd_include_context_data *)cmd->data;

	if (ctx_data->location_assigned) {
		sieve_argument_validate_error(
			valdtr, *arg,
			"include: cannot use location tags ':personal' and ':global' "
			"multiple times");
		return FALSE;
	}

	if (sieve_argument_is(*arg, include_personal_tag))
		ctx_data->location = EXT_INCLUDE_LOCATION_PERSONAL;
	else if (sieve_argument_is(*arg, include_global_tag))
		ctx_data->location = EXT_INCLUDE_LOCATION_GLOBAL;
	else
		return FALSE;

	ctx_data->location_assigned = TRUE;

	/* Delete this tag (for now) */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	return TRUE;
}

static bool
cmd_include_validate_boolean_tag(struct sieve_validator *valdtr ATTR_UNUSED,
				 struct sieve_ast_argument **arg,
				 struct sieve_command *cmd)
{
	struct cmd_include_context_data *ctx_data =
		(struct cmd_include_context_data *)cmd->data;

	if (sieve_argument_is(*arg, include_once_tag))
		ctx_data->flags |= EXT_INCLUDE_FLAG_ONCE;
	else
		ctx_data->flags |= EXT_INCLUDE_FLAG_OPTIONAL;

	/* Delete this tag (for now) */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	return TRUE;
}

/*
 * Command registration
 */

static bool
cmd_include_registered(struct sieve_validator *valdtr,
		       const struct sieve_extension *ext,
		       struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &include_personal_tag, 0);
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &include_global_tag, 0);
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &include_once_tag, 0);
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &include_optional_tag, 0);

	return TRUE;
}

/*
 * Command validation
 */

static bool
cmd_include_pre_validate(struct sieve_validator *valdtr ATTR_UNUSED,
			 struct sieve_command *cmd)
{
	struct cmd_include_context_data *ctx_data;

	/* Assign context */
	ctx_data = p_new(sieve_command_pool(cmd),
			 struct cmd_include_context_data, 1);
	ctx_data->location = EXT_INCLUDE_LOCATION_PERSONAL;
	cmd->data = ctx_data;

	return TRUE;
}

static bool
cmd_include_validate(struct sieve_validator *valdtr,
		     struct sieve_command *cmd)
{
	const struct sieve_extension *this_ext = cmd->ext;
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct cmd_include_context_data *ctx_data =
		(struct cmd_include_context_data *)cmd->data;
	struct sieve_script *script;
	const char *script_name;
	enum sieve_error error_code = SIEVE_ERROR_NONE;

	/* Check argument */
	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "value",
						1, SAAT_STRING))
		return FALSE;

	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;

	/*
	 * Variables are not allowed.
	 */
	if (!sieve_argument_is_string_literal(arg)) {
		sieve_argument_validate_error(
			valdtr, arg,
			"the include command requires a constant string for its value argument");
		return FALSE;
	}

	/* Find the script */

	script_name = sieve_ast_argument_strc(arg);

	if (!sieve_script_name_is_valid(script_name)) {
 		sieve_argument_validate_error(
			valdtr, arg, "include: invalid script name '%s'",
			str_sanitize(script_name, 80));
		return FALSE;
	}

	/* Open script */
	if (ext_include_open_script(this_ext, ctx_data->location,
				    script_name, &script, &error_code) < 0) {
		if (error_code != SIEVE_ERROR_NOT_FOUND) {
			sieve_argument_validate_error(
				valdtr, arg,
				"failed to access included %s script '%s' "
				"(refer to server log for more information)",
				ext_include_script_location_name(ctx_data->location),
				str_sanitize(script_name, 80));
			return FALSE;
		/* Not found */
		} else {
			enum sieve_compile_flags cpflags =
				sieve_validator_compile_flags(valdtr);

			if ((ctx_data->flags & EXT_INCLUDE_FLAG_OPTIONAL) != 0) {
				/* :optional */
			} else if ((cpflags & SIEVE_COMPILE_FLAG_UPLOADED) != 0) {
				/* Script is being uploaded */
				sieve_argument_validate_warning(
					valdtr, arg,
					"included %s script '%s' does not exist (ignored during upload)",
					ext_include_script_location_name(ctx_data->location),
					str_sanitize(script_name, 80));
				ctx_data->flags |= EXT_INCLUDE_FLAG_MISSING_AT_UPLOAD;

			} else {
				/* Should have existed */
				sieve_argument_validate_error(
					valdtr, arg,
					"included %s script '%s' does not exist",
					ext_include_script_location_name(ctx_data->location),
					str_sanitize(script_name, 80));
				return FALSE;
			}
		}
	}

	ext_include_ast_link_included_script(cmd->ext, cmd->ast_node->ast, script);
	ctx_data->script_name = p_strdup(sieve_command_pool(cmd), script_name);
	ctx_data->script = script;

	(void)sieve_ast_arguments_detach(arg, 1);
	return TRUE;
}

/*
 * Code Generation
 */

static bool
cmd_include_generate(const struct sieve_codegen_env *cgenv,
		     struct sieve_command *cmd)
{
	struct cmd_include_context_data *ctx_data =
		(struct cmd_include_context_data *)cmd->data;
	const struct ext_include_script_info *included;
	int ret;

	/* Compile (if necessary) and include the script into the binary.
	   This yields the id of the binary block containing the compiled byte
	   code. */
	ret = ext_include_generate_include(cgenv, cmd, ctx_data->location,
					   ctx_data->script_name,
					   ctx_data->flags, ctx_data->script,
					   &included);
	if (ret < 0)
 		return FALSE;
	if (ret > 0) {
	 	(void)sieve_operation_emit(cgenv->sblock, cmd->ext,
					   &include_operation);
		(void)sieve_binary_emit_unsigned(cgenv->sblock, included->id);
		(void)sieve_binary_emit_byte(cgenv->sblock, ctx_data->flags);
	}
	return TRUE;
}

/*
 * Code dump
 */

static bool opc_include_dump(const struct sieve_dumptime_env *denv,
			     sieve_size_t *address)
{
	const struct ext_include_script_info *included;
	struct ext_include_binary_context *binctx;
	unsigned int include_id, flags;

	sieve_code_dumpf(denv, "INCLUDE:");

	sieve_code_mark(denv);
	if (!sieve_binary_read_unsigned(denv->sblock, address, &include_id))
		return FALSE;
	if (!sieve_binary_read_byte(denv->sblock, address, &flags))
		return FALSE;

	binctx = ext_include_binary_get_context(denv->oprtn->ext, denv->sbin);
	included = ext_include_binary_script_get_included(binctx, include_id);
	if (included == NULL || included->block == NULL)
		return FALSE;

	sieve_code_descend(denv);
	sieve_code_dumpf(
		denv, "script: '%s' %s%s[ID: %d, BLOCK: %d]",
		sieve_script_label(included->script),
		((flags & EXT_INCLUDE_FLAG_ONCE) != 0 ? "(once) " : ""),
		((flags & EXT_INCLUDE_FLAG_OPTIONAL) != 0 ? "(optional) " : ""),
		include_id, sieve_binary_block_get_id(included->block));

	return TRUE;
}

/*
 * Execution
 */

static int
opc_include_execute(const struct sieve_runtime_env *renv,
		    sieve_size_t *address)
{
	unsigned int include_id, flags;

	if (!sieve_binary_read_unsigned(renv->sblock, address, &include_id)) {
		sieve_runtime_trace_error(renv, "invalid include-id operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	if (!sieve_binary_read_unsigned(renv->sblock, address, &flags)) {
		sieve_runtime_trace_error(renv, "invalid flags operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	return ext_include_execute_include(renv, include_id,
					   (enum ext_include_flags)flags);
}
