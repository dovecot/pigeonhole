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
#include "sieve-message.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "sieve-ext-variables.h"

#include "ext-mime-common.h"

/*
 * Extracttext command
 *
 * Syntax:
 *    extracttext [MODIFIER] [":first" number] <varname: string>
 */

static bool cmd_extracttext_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool cmd_extracttext_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool cmd_extracttext_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

const struct sieve_command_def cmd_extracttext = {
	.identifier = "extracttext",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = cmd_extracttext_registered,
	.validate = cmd_extracttext_validate,
	.generate = cmd_extracttext_generate
};

/*
 * Extracttext command tags
 */

enum cmd_extracttext_optional {
	CMD_EXTRACTTEXT_OPT_END,
	CMD_EXTRACTTEXT_OPT_FIRST
};

static bool cmd_extracttext_validate_first_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);

static const struct sieve_argument_def extracttext_from_tag = {
	.identifier = "first",
	.validate = cmd_extracttext_validate_first_tag
};

/*
 * Extracttext operation
 */

static bool cmd_extracttext_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_extracttext_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def extracttext_operation = {
	.mnemonic = "EXTRACTTEXT",
	.ext_def = &extracttext_extension,
	.dump = cmd_extracttext_operation_dump,
	.execute = cmd_extracttext_operation_execute
};

/*
 * Compiler context
 */

struct cmd_extracttext_context {
	ARRAY_TYPE(sieve_variables_modifier) modifiers;
};

/*
 * Tag validation
 */

static bool cmd_extracttext_validate_first_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
	struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Check syntax:
	 *   :first <number>
	 */
	if ( !sieve_validate_tag_parameter
		(valdtr, cmd, tag, *arg, NULL, 0, SAAT_NUMBER, FALSE) )
		return FALSE;

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/* Command registration */

static bool cmd_extracttext_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg)
{
	struct ext_extracttext_context *ectx =
		(struct ext_extracttext_context *)ext->context;

	sieve_validator_register_tag(valdtr, cmd_reg, ext,
		&extracttext_from_tag, CMD_EXTRACTTEXT_OPT_FIRST);
	sieve_variables_modifiers_link_tag
		(valdtr, ectx->var_ext, cmd_reg);
	return TRUE;
}

/*
 * Command validation
 */

static bool cmd_extracttext_validate(struct sieve_validator *valdtr,
	struct sieve_command *cmd)
{
	const struct sieve_extension *this_ext = cmd->ext;
	struct ext_extracttext_context *ectx =
		(struct ext_extracttext_context *)this_ext->context;
	struct sieve_ast_node *node = cmd->ast_node;
	struct sieve_ast_argument *arg = cmd->first_positional;
	pool_t pool = sieve_command_pool(cmd);
	struct cmd_extracttext_context *sctx;

	/* Create command context */
	sctx = p_new(pool, struct cmd_extracttext_context, 1);
	p_array_init(&sctx->modifiers, pool, 4);
	cmd->data = (void *) sctx;

	/* Validate modifiers */
	if ( !sieve_variables_modifiers_validate
		(valdtr, cmd, &sctx->modifiers) )
		return FALSE;

	/* Validate varname argument */
	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "varname", 1, SAAT_STRING) ) {
		return FALSE;
	}
	if ( !sieve_variable_argument_activate
		(ectx->var_ext, ectx->var_ext, valdtr, cmd, arg, TRUE) )
		return FALSE;

	/* Check foreverypart context */
	i_assert(node != NULL);
	while ( node != NULL ) {
		if ( node->command != NULL &&
			sieve_command_is(node->command, cmd_foreverypart) )
			break;
		node = sieve_ast_node_parent(node);
	}

	if ( node == NULL ) {
		sieve_command_validate_error(valdtr, cmd,
			"the extracttext command is not placed inside "
			"a foreverypart loop");
		return FALSE;
	}
	return TRUE;
}

/*
 * Code generation
 */

static bool cmd_extracttext_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	const struct sieve_extension *this_ext = cmd->ext;
	struct sieve_binary_block *sblock = cgenv->sblock;
	struct cmd_extracttext_context *sctx =
		(struct cmd_extracttext_context *) cmd->data;

	sieve_operation_emit(sblock, this_ext, &extracttext_operation);

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

static bool cmd_extracttext_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "EXTRACTTEXT");
	sieve_code_descend(denv);

	/* Dump optional operands */

	for (;;) {
		int opt;
		bool opok = TRUE;

		if ( (opt=sieve_opr_optional_dump(denv, address, &opt_code)) < 0 )
			return FALSE;
		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case CMD_EXTRACTTEXT_OPT_FIRST:
			opok = sieve_opr_number_dump(denv, address, "first");
			break;
		default:
			return FALSE;
		}
		if ( !opok ) return FALSE;
	}

	/* Print both variable name and string value */
	if ( !sieve_opr_string_dump(denv, address, "varname") )
		return FALSE;

	return sieve_variables_modifiers_code_dump(denv, address);
}

/*
 * Code execution
 */

static int cmd_extracttext_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct ext_extracttext_context *ectx =
		(struct ext_extracttext_context *)this_ext->context;
	struct sieve_variable_storage *storage;
	ARRAY_TYPE(sieve_variables_modifier) modifiers;
	struct ext_foreverypart_runtime_loop *sfploop;
	struct sieve_message_part *mpart;
	struct sieve_message_part_data mpart_data;
	int opt_code = 0;
	sieve_number_t first = 0;
	string_t *value;
	unsigned int var_index;
	bool have_first = FALSE;
	int ret = SIEVE_EXEC_OK;

	/*
	 * Read the normal operands
	 */

	/* Optional operands */

	for (;;) {
		int opt;

		if ( (opt=sieve_opr_optional_read
			(renv, address, &opt_code)) < 0 )
			return SIEVE_EXEC_BIN_CORRUPT;
		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case CMD_EXTRACTTEXT_OPT_FIRST:
			ret = sieve_opr_number_read
				(renv, address, "first", &first);
			have_first = TRUE;
			break;
		default:
			sieve_runtime_trace_error(renv, "unknown optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
		if ( ret <= 0 ) return ret;
	}

	/* Varname operand */

	if ( (ret=sieve_variable_operand_read
		(renv, address, "varname", &storage, &var_index)) <= 0 )
		return ret;

	/* Modifiers */

	if ( (ret=sieve_variables_modifiers_code_read
		(renv, address, &modifiers)) <= 0 )
		return ret;

	/*
	 * Determine and assign the value
	 */

	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS, "extracttext command");
	sieve_runtime_trace_descend(renv);

	sfploop = ext_foreverypart_runtime_loop_get_current(renv);
	if ( sfploop == NULL ) {
		sieve_runtime_trace_error(renv,
			"outside foreverypart context");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Get current message part */
	mpart = sieve_message_part_iter_current(&sfploop->part_iter);
	i_assert( mpart != NULL );

	/* Get message part content */
	sieve_message_part_get_data(mpart, &mpart_data, TRUE);

	/* Apply ":first" limit, if any */
	if ( !have_first || (size_t)first > mpart_data.size ) {
		value = t_str_new_const(mpart_data.content, mpart_data.size);
	} else {
		value = t_str_new((size_t)first);
		str_append_n(value, mpart_data.content, (size_t)first);
	}

	/* Apply modifiers */
	if ( (ret=sieve_variables_modifiers_apply
		(renv, ectx->var_ext, &modifiers, &value)) <= 0 )
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






