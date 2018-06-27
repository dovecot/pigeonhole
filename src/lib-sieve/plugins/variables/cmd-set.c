/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-extensions.h"

#include "sieve-code.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-binary.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-variables-common.h"

/*
 * Set command
 *
 * Syntax:
 *    set [MODIFIER] <name: string> <value: string>
 */

static bool cmd_set_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool cmd_set_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool cmd_set_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

const struct sieve_command_def cmd_set = {
	.identifier = "set",
	.type = SCT_COMMAND,
	.positional_args = 2,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = cmd_set_registered,
	.validate = cmd_set_validate,
	.generate = cmd_set_generate,
};

/*
 * Set operation
 */

static bool cmd_set_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_set_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def cmd_set_operation = {
	.mnemonic = "SET",
	.ext_def = &variables_extension,
	.code = EXT_VARIABLES_OPERATION_SET,
	.dump = cmd_set_operation_dump,
	.execute = cmd_set_operation_execute
};

/*
 * Compiler context
 */

struct cmd_set_context {
	ARRAY_TYPE(sieve_variables_modifier) modifiers;
};

/* Command registration */

static bool cmd_set_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg)
{
	sieve_variables_modifiers_link_tag(valdtr, ext, cmd_reg);

	return TRUE;
}

/*
 * Command validation
 */

static bool cmd_set_validate(struct sieve_validator *valdtr,
	struct sieve_command *cmd)
{
	const struct sieve_extension *this_ext = cmd->ext;
	struct sieve_ast_argument *arg = cmd->first_positional;
	pool_t pool = sieve_command_pool(cmd);
	struct cmd_set_context *sctx;

	/* Create command context */
	sctx = p_new(pool, struct cmd_set_context, 1);
	p_array_init(&sctx->modifiers, pool, 4);
	cmd->data = (void *) sctx;

	/* Validate modifiers */
	if ( !sieve_variables_modifiers_validate
		(valdtr, cmd, &sctx->modifiers) )
		return FALSE;

	/* Validate name argument */
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "name", 1, SAAT_STRING) ) {
		return FALSE;
	}
	if ( !sieve_variable_argument_activate
		(this_ext, this_ext, valdtr, cmd, arg, TRUE) ) {
		return FALSE;
	}
	arg = sieve_ast_argument_next(arg);

	/* Validate value argument */
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "value", 2, SAAT_STRING) ) {
		return FALSE;
	}
	return sieve_validator_argument_activate
		(valdtr, cmd, arg, FALSE);
}

/*
 * Code generation
 */

static bool cmd_set_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	const struct sieve_extension *this_ext = cmd->ext;
	struct sieve_binary_block *sblock = cgenv->sblock;
	struct cmd_set_context *sctx = (struct cmd_set_context *) cmd->data;

	sieve_operation_emit(sblock, this_ext, &cmd_set_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;

	/* Generate modifiers */
	if ( !sieve_variables_modifiers_generate
		(cgenv, &sctx->modifiers) )
		return FALSE;

	return TRUE;
}

/*
 * Code dump
 */

static bool cmd_set_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "SET");
	sieve_code_descend(denv);

	/* Print both variable name and string value */
	if ( !sieve_opr_string_dump(denv, address, "variable") ||
		!sieve_opr_string_dump(denv, address, "value") )
		return FALSE;

	return sieve_variables_modifiers_code_dump(denv, address);
}

/*
 * Code execution
 */

static int cmd_set_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct sieve_variable_storage *storage;
	ARRAY_TYPE(sieve_variables_modifier) modifiers;
	unsigned int var_index;
	string_t *value;
	int ret = SIEVE_EXEC_OK;

	/*
	 * Read the normal operands
	 */

	if ( (ret=sieve_variable_operand_read
		(renv, address, "variable", &storage, &var_index)) <= 0 )
		return ret;

	if ( (ret=sieve_opr_string_read(renv, address, "string", &value)) <= 0 )
		return ret;

	if ( (ret=sieve_variables_modifiers_code_read
		(renv, this_ext, address, &modifiers)) <= 0 )
		return ret;

	/*
	 * Determine and assign the value
	 */

	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS, "set command");
	sieve_runtime_trace_descend(renv);

	/* Apply modifiers */
	if ( (ret=sieve_variables_modifiers_apply
		(renv, this_ext, &modifiers, &value)) <= 0 )
		return ret;

	/* Actually assign the value if all is well */
	i_assert ( value != NULL );
	if ( !sieve_variable_assign(storage, var_index, value) )
		return SIEVE_EXEC_BIN_CORRUPT;

	/* Trace */
	if ( sieve_runtime_trace_active(renv, SIEVE_TRLVL_COMMANDS) ) {
		const char *var_name, *var_id;

		(void)sieve_variable_get_identifier(storage, var_index, &var_name);
		var_id = sieve_variable_get_varid(storage, var_index);

		sieve_runtime_trace_here(renv, 0, "assign `%s' [%s] = \"%s\"",
			var_name, var_id, str_c(value));
	}

	return SIEVE_EXEC_OK;
}






