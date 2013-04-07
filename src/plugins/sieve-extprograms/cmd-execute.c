/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "buffer.h"
#include "str.h"
#include "str-sanitize.h"
#include "istream.h"
#include "ostream.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-binary.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"

#include "sieve-ext-variables.h"

#include "sieve-extprograms-common.h"

/* Execute command
 *
 * Syntax:
 *   "execute" [":input" <input-data: string> / ":pipe"] 
 *             [":output" <varname: string>]
 *             <program-name: string> [<arguments: string-list>]
 *
 */

static bool cmd_execute_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool cmd_execute_generate
	(const struct sieve_codegen_env *cgenv, 
		struct sieve_command *ctx);

const struct sieve_command_def cmd_execute = {
	"execute",
	SCT_HYBRID,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE,
	cmd_execute_registered,
	NULL,
	sieve_extprogram_command_validate,
	NULL,
	cmd_execute_generate,
	NULL,
};

/*
 * Tagged arguments
 */

static bool cmd_execute_validate_input_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
    struct sieve_command *cmd);
static bool cmd_execute_generate_input_tag
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
    struct sieve_command *cmd);

static bool cmd_execute_validate_output_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
		struct sieve_command *cmd);

static const struct sieve_argument_def execute_input_tag = { 
	"input", 
	NULL, 
	cmd_execute_validate_input_tag,
	NULL, NULL,
	cmd_execute_generate_input_tag
};

static const struct sieve_argument_def execute_pipe_tag = { 
	"pipe", 
	NULL, 
	cmd_execute_validate_input_tag,
	NULL, NULL,
	cmd_execute_generate_input_tag
};

static const struct sieve_argument_def execute_output_tag = { 
	"output", 
	NULL, 
	cmd_execute_validate_output_tag,
	NULL, NULL, NULL 
};


/* 
 * Execute operation 
 */

static bool cmd_execute_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_execute_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def cmd_execute_operation = { 
	"EXECUTE", &execute_extension, 
	0,
	cmd_execute_operation_dump, 
	cmd_execute_operation_execute
};

/* Codes for optional operands */

enum cmd_execute_optional {
  OPT_END,
  OPT_INPUT,
  OPT_OUTPUT
};

/*
 * Tag validation
 */

static bool cmd_execute_validate_input_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
    struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	if ( (bool) cmd->data ) {
		sieve_argument_validate_error(valdtr, *arg,
			"multiple :input or :pipe arguments specified for the %s %s",
			sieve_command_identifier(cmd), sieve_command_type_name(cmd));
		return FALSE;
	}

	cmd->data = (void *) TRUE;

	/* Skip tag */
 	*arg = sieve_ast_argument_next(*arg);

	if ( sieve_argument_is(tag, execute_input_tag) ) {
		/* Check syntax:
		 *   :input <input-data: string>
		 */
		if ( !sieve_validate_tag_parameter
			(valdtr, cmd, tag, *arg, NULL, 0, SAAT_STRING, FALSE) ) {
			return FALSE;
		}

		/* Assign tag parameters */
		tag->parameters = *arg;
		*arg = sieve_ast_arguments_detach(*arg,1);
	}

	return TRUE;
}

static bool cmd_execute_validate_output_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
	struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct sieve_extprograms_config *ext_config =
		(struct sieve_extprograms_config *) cmd->ext->context;

	if ( ext_config == NULL || ext_config->var_ext == NULL ||
		!sieve_ext_variables_is_active(ext_config->var_ext, valdtr) )	{
		sieve_argument_validate_error(valdtr,*arg, 
			"the %s %s only allows for the specification of an "
			":output argument when the variables extension is active",
			sieve_command_identifier(cmd), sieve_command_type_name(cmd));
		return FALSE;
	}

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg, 1);
	
	if ( !sieve_variable_argument_activate
		(ext_config->var_ext, valdtr, cmd, *arg, TRUE) )
		return FALSE;

	(*arg)->argument->id_code = tag->argument->id_code;

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);
					
	return TRUE;
}		

/*
 * Command registration
 */

static bool cmd_execute_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &execute_input_tag, OPT_INPUT);
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &execute_pipe_tag, OPT_INPUT);
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &execute_output_tag, OPT_OUTPUT);
	return TRUE;
}

/*
 * Code generation
 */

static bool cmd_execute_generate_input_tag
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
    struct sieve_command *cmd)
{
	if ( arg->parameters == NULL ) {
		sieve_opr_omitted_emit(cgenv->sblock);
		return TRUE;
	}

	return sieve_generate_argument_parameters(cgenv, cmd, arg);
}

static bool cmd_execute_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &cmd_execute_operation);

	/* Emit is_test flag */
	sieve_binary_emit_byte(cgenv->sblock, ( cmd->ast_node->type == SAT_TEST ));

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;

	/* Emit a placeholder when the <arguments> argument is missing */
	if ( sieve_ast_argument_next(cmd->first_positional) == NULL )
		sieve_opr_omitted_emit(cgenv->sblock);

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_execute_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{	
	int opt_code = 0;
	unsigned int is_test = 0;
	
	/* Read is_test flag */
	if ( !sieve_binary_read_byte(denv->sblock, address, &is_test) )
		return FALSE;

	sieve_code_dumpf(denv, "EXECUTE (%s)", (is_test ? "test" : "command"));
	sieve_code_descend(denv);	

	/* Dump optional operands */
	for (;;) {
		int opt;
		bool opok = TRUE;

		if ( (opt=sieve_action_opr_optional_dump(denv, address, &opt_code)) < 0 )
			return FALSE;

		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case OPT_INPUT:
			opok = sieve_opr_string_dump_ex(denv, address, "input", "PIPE");
			break;	
		case OPT_OUTPUT:
			opok = sieve_opr_string_dump(denv, address, "output");
			break;
		default:
			return FALSE;
		}

		if ( !opok ) return FALSE;
	}
	
	if ( !sieve_opr_string_dump(denv, address, "program-name") )
		return FALSE;

	return sieve_opr_stringlist_dump_ex(denv, address, "arguments", "");
}

/* 
 * Code execution
 */

static int cmd_execute_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct sieve_side_effects_list *slist = NULL;
	int opt_code = 0;
	unsigned int is_test = 0;
	struct sieve_stringlist *args_list = NULL;
	string_t *pname = NULL, *input = NULL;
	struct sieve_variable_storage *var_storage = NULL;
	unsigned int var_index;
	bool have_input = FALSE;
	const char *program_name = NULL;
	const char *const *args = NULL;
	enum sieve_error error = SIEVE_ERROR_NONE;
	buffer_t *outbuf = NULL;
	struct sieve_extprogram *sprog = NULL;
	int ret;

	/*
	 * Read operands
	 */

	/* The is_test flag */
	if ( !sieve_binary_read_byte(renv->sblock, address, &is_test) ) {
		sieve_runtime_trace_error(renv, "invalid is_test flag");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Optional operands */	

	for (;;) {
		int opt;

		if ( (opt=sieve_action_opr_optional_read
			(renv, address, &opt_code, &ret, &slist)) < 0 )
			return ret;

		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case OPT_INPUT:
			if ( (ret=sieve_opr_string_read_ex
				(renv, address, "input", TRUE, &input, NULL)) <= 0 )
				return ret;
			have_input = TRUE;
			break;
		case OPT_OUTPUT:
			if ( (ret=sieve_variable_operand_read
				(renv, address, "output", &var_storage, &var_index)) <= 0 )
				return ret;
			break;
		default:
			sieve_runtime_trace_error(renv, "unknown optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}

		if ( ret <= 0 ) return ret;
	}

	/* Fixed operands */

	if ( (ret=sieve_extprogram_command_read_operands
		(renv, address, &pname, &args_list)) <= 0 )
		return ret;

	program_name = str_c(pname);
	if ( args_list != NULL &&
		sieve_stringlist_read_all(args_list, pool_datastack_create(), &args) < 0 ) {
		sieve_runtime_trace_error(renv, "failed to read args operand");
		return args_list->exec_status;
	}

	/*
	 * Perform operation
	 */

	/* Trace */

	sieve_runtime_trace(renv, SIEVE_TRLVL_ACTIONS, "execute action");
	sieve_runtime_trace_descend(renv);
	sieve_runtime_trace(renv, SIEVE_TRLVL_ACTIONS,
		"execute program `%s'", str_sanitize(program_name, 128));

	sprog = sieve_extprogram_create
		(this_ext, renv->scriptenv, renv->msgdata, "execute", program_name, args,
			&error);
	if ( sprog != NULL ) {
		if ( var_storage != NULL ) {
			// FIXME: limit output size
			struct ostream *outdata;

			outbuf = buffer_create_dynamic(pool_datastack_create(), 1024);
			outdata = o_stream_create_buffer(outbuf);
			sieve_extprogram_set_output(sprog, outdata);
			o_stream_unref(&outdata);
		}

		if ( input == NULL && have_input ) {
			ret = sieve_extprogram_set_input_mail
				(sprog, sieve_message_get_mail(renv->msgctx));
		} else if ( input != NULL ) {
			struct istream *indata =
				i_stream_create_from_data(str_data(input), str_len(input));
			sieve_extprogram_set_input(sprog, indata);
			i_stream_unref(&indata);
			ret = 1;
		}

		if ( ret >= 0 ) 
			ret = sieve_extprogram_run(sprog);
		sieve_extprogram_destroy(&sprog);
	} else {
		ret = -1;
	}

	if ( ret > 0 ) {
		sieve_runtime_trace(renv,	SIEVE_TRLVL_ACTIONS,
			"executed program successfully");

		if ( var_storage != NULL ) {
			string_t *var;

			if ( sieve_variable_get_modifiable(var_storage, var_index, &var) ) {
				str_truncate(var, 0);
				str_append_str(var, outbuf); 

				sieve_runtime_trace(renv,	SIEVE_TRLVL_ACTIONS,
					"assigned output variable");
			} // FIXME: handle failure
		}

	} else if ( ret < 0 ) {
		if ( error == SIEVE_ERROR_NOT_FOUND ) {
			sieve_runtime_error(renv, NULL,
				"execute action: program `%s' not found",
				str_sanitize(program_name, 80));
		} else {
			sieve_extprogram_exec_error(renv->ehandler,
				sieve_runtime_get_full_command_location(renv),
				"execute action: failed to execute to program `%s'",
				str_sanitize(program_name, 80));
		}
	} else {
		sieve_runtime_trace(renv,	SIEVE_TRLVL_ACTIONS,
			"execute action: program indicated false result");
	}

	if ( outbuf != NULL ) 
		buffer_free(&outbuf);

	if ( is_test )
		sieve_interpreter_set_test_result(renv->interp, ( ret > 0 ));

	return SIEVE_EXEC_OK;
}

