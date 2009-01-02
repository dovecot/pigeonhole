/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"

#include "ext-enotify-common.h"

/* 
 * Forward declarations 
 */
 
static const struct sieve_argument notify_importance_tag;
static const struct sieve_argument notify_from_tag;
static const struct sieve_argument notify_options_tag;
static const struct sieve_argument notify_message_tag;

/* 
 * Notify command 
 *	
 * Syntax: 
 *    notify [":from" string]
 *           [":importance" <"1" / "2" / "3">]
 *           [":options" string-list]
 *           [":message" string]
 *           <method: string>
 */

static bool cmd_notify_registered
	(struct sieve_validator *valdtr, 
		struct sieve_command_registration *cmd_reg);
static bool cmd_notify_pre_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_notify_validate
	(struct sieve_validator *valdtr, struct sieve_command_context *cmd);
static bool cmd_notify_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command notify_command = { 
	"notify",
	SCT_COMMAND, 
	1, 0, FALSE, FALSE, 
	cmd_notify_registered,
	cmd_notify_pre_validate,
	cmd_notify_validate, 
	cmd_notify_generate, 
	NULL 
};

/*
 * Notify command tags
 */

/* Forward declarations */

static bool cmd_notify_validate_string_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);
static bool cmd_notify_validate_stringlist_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);
static bool cmd_notify_validate_importance_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);

/* Argument objects */

static const struct sieve_argument notify_from_tag = { 
	"from", 
	NULL, NULL,
	cmd_notify_validate_string_tag, 
	NULL, NULL 
};

static const struct sieve_argument notify_options_tag = { 
	"options", 
	NULL, NULL,
	cmd_notify_validate_stringlist_tag, 
	NULL, NULL 
};

static const struct sieve_argument notify_message_tag = { 
	"message", 
	NULL, NULL, 
	cmd_notify_validate_string_tag, 
	NULL, NULL 
};

static const struct sieve_argument notify_importance_tag = { 
	"importance", 
	NULL, NULL,
	cmd_notify_validate_importance_tag, 
	NULL, NULL 
};

/* Codes for optional arguments */

enum cmd_notify_optional {
	OPT_END,
	OPT_FROM,
	OPT_OPTIONS,
	OPT_MESSAGE,
	OPT_IMPORTANCE
};

/* 
 * Notify operation 
 */

static bool cmd_notify_operation_dump
	(const struct sieve_operation *op,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_notify_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation notify_operation = { 
	"NOTIFY",
	&enotify_extension,
	EXT_ENOTIFY_OPERATION_NOTIFY,
	cmd_notify_operation_dump, 
	cmd_notify_operation_execute
};

/* 
 * Notify action 
 */

/* Forward declarations */

static int act_notify_check_duplicate
	(const struct sieve_runtime_env *renv, 
		const struct sieve_action_data *act,
		const struct sieve_action_data *act_other);
static void act_notify_print
	(const struct sieve_action *action, const struct sieve_result_print_env *rpenv,
		void *context, bool *keep);	
static bool act_notify_commit
	(const struct sieve_action *action,	const struct sieve_action_exec_env *aenv, 
		void *tr_context, bool *keep);

/* Action object */

const struct sieve_action act_notify = {
	"notify",
	0,
	act_notify_check_duplicate, 
	NULL,
	act_notify_print,
	NULL, NULL,
	act_notify_commit,
	NULL
};

/*
 * Command validation context
 */
 
struct cmd_notify_context_data {
	struct sieve_ast_argument *from;
	struct sieve_ast_argument *message;
	struct sieve_ast_argument *options;
};

/* 
 * Tag validation 
 */

static bool cmd_notify_validate_string_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct cmd_notify_context_data *ctx_data = 
		(struct cmd_notify_context_data *) cmd->data; 

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);
	
	/* Check syntax:
	 *   :from <string>
	 *   :message <string>
	 */
	if ( !sieve_validate_tag_parameter(valdtr, cmd, tag, *arg, SAAT_STRING) )
		return FALSE;

	if ( tag->argument == &notify_from_tag ) {
		ctx_data->from = *arg;
		
		/* Skip parameter */
		*arg = sieve_ast_argument_next(*arg);
		
	} else if ( tag->argument == &notify_message_tag ) {
		ctx_data->message = *arg;

		/* Skip parameter */
		*arg = sieve_ast_argument_next(*arg);	
	}
			
	return TRUE;
}

static bool cmd_notify_validate_stringlist_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct cmd_notify_context_data *ctx_data = 
		(struct cmd_notify_context_data *) cmd->data; 

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);
	
	/* Check syntax:
	 *   :options string-list
	 */
	if ( !sieve_validate_tag_parameter(valdtr, cmd, tag, *arg, SAAT_STRING_LIST) ) 
		return FALSE;
		
	/* Assign context */
	ctx_data->options = *arg;	
	
	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool cmd_notify_validate_importance_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	const struct sieve_ast_argument *tag = *arg;
	const char *impstr;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Check syntax: 
	 *   :importance <"1" / "2" / "3">
	 */

	if ( sieve_ast_argument_type(*arg) != SAAT_STRING ) {
		/* Not a string */
		sieve_argument_validate_error(valdtr, *arg, 
			"the :importance tag for the notify command requires a string parameter, "
			"but %s was found", sieve_ast_argument_name(*arg));
		return FALSE;
	}

	impstr = sieve_ast_argument_strc(*arg);

	if ( impstr[0] < '1' || impstr[0]  > '3' || impstr[1] != '\0' ) {
		/* Invalid importance */
		sieve_argument_validate_error(valdtr, *arg, 
			"invalid :importance value for notify command: %s", impstr);
		return FALSE;
	} 

	sieve_ast_argument_number_substitute(*arg, impstr[0] - '0');
	(*arg)->arg_id_code = tag->arg_id_code;
	(*arg)->argument = &number_argument;

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);
			
	return TRUE;
}


/* 
 * Command registration 
 */

static bool cmd_notify_registered
(struct sieve_validator *valdtr, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag
		(valdtr, cmd_reg, &notify_importance_tag, OPT_IMPORTANCE); 	
	sieve_validator_register_tag
		(valdtr, cmd_reg, &notify_from_tag, OPT_FROM); 	
	sieve_validator_register_tag
		(valdtr, cmd_reg, &notify_options_tag, OPT_OPTIONS); 	
	sieve_validator_register_tag
		(valdtr, cmd_reg, &notify_message_tag, OPT_MESSAGE); 	

	return TRUE;
}

/* 
 * Command validation 
 */

static bool cmd_notify_pre_validate
(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_command_context *cmd) 
{
	struct cmd_notify_context_data *ctx_data;
	
	/* Assign context */
	ctx_data = p_new(sieve_command_pool(cmd), 
		struct cmd_notify_context_data, 1);
	cmd->data = ctx_data;

	return TRUE;
}
 
static bool cmd_notify_validate
(struct sieve_validator *valdtr, struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct cmd_notify_context_data *ctx_data = 
		(struct cmd_notify_context_data *) cmd->data; 

	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "method", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(valdtr, cmd, arg, FALSE) )
		return FALSE;
		
	return ext_enotify_compile_check_arguments
		(valdtr, arg, ctx_data->message, ctx_data->from, ctx_data->options);
}

/*
 * Code generation
 */
 
static bool cmd_notify_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{		 
	sieve_operation_emit_code(cgenv->sbin, &notify_operation);

	/* Emit source line */
	sieve_code_source_line_emit(cgenv->sbin, sieve_command_source_line(ctx));

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, ctx, NULL);
}

/* 
 * Code dump
 */
 
static bool cmd_notify_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{	
	int opt_code = 1;
	
	sieve_code_dumpf(denv, "NOTIFY");
	sieve_code_descend(denv);	

	/* Source line */
	if ( !sieve_code_source_line_dump(denv, address) )
		return FALSE;

	/* Dump optional operands */
	if ( sieve_operand_optional_present(denv->sbin, address) ) {
		while ( opt_code != 0 ) {
			sieve_code_mark(denv);
			
			if ( !sieve_operand_optional_read(denv->sbin, address, &opt_code) ) 
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_IMPORTANCE:
				if ( !sieve_opr_number_dump(denv, address, "importance") )
					return FALSE;
				break;
			case OPT_FROM:
				if ( !sieve_opr_string_dump(denv, address, "from") )
					return FALSE;
				break;
			case OPT_OPTIONS:
				if ( !sieve_opr_stringlist_dump(denv, address, "options") )
					return FALSE;
				break;
			case OPT_MESSAGE:
				if ( !sieve_opr_string_dump(denv, address, "message") )
					return FALSE;
				break;
			default:
				return FALSE;
			}
		}
	}
	
	/* Dump reason and handle operands */
	return 
		sieve_opr_string_dump(denv, address, "method");
}

/* 
 * Code execution
 */
 
static int cmd_notify_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	struct sieve_side_effects_list *slist = NULL;
	struct sieve_enotify_action *act;
	void *method_context;
	pool_t pool;
	int opt_code = 1, result = SIEVE_EXEC_OK;
	sieve_number_t importance = 1;
	struct sieve_coded_stringlist *options = NULL;
	const struct sieve_enotify_method *method;
	string_t *method_uri, *message = NULL, *from = NULL; 
	unsigned int source_line;

	/*
	 * Read operands
	 */
		
	/* Source line */
	if ( !sieve_code_source_line_read(renv, address, &source_line) ) {
		sieve_runtime_trace_error(renv, "invalid source line");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/* Optional operands */	
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) ) {
				sieve_runtime_trace_error(renv, "invalid optional operand");
				return SIEVE_EXEC_BIN_CORRUPT;
			}

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_IMPORTANCE:
				if ( !sieve_opr_number_read(renv, address, &importance) ) {
					sieve_runtime_trace_error(renv, "invalid importance operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
	
				/* Enforce 0 < importance < 4 (just to be sure) */
				if ( importance < 1 ) 
					importance = 1;
				else if ( importance > 3 )
					importance = 3;
				break;
			case OPT_FROM:
				if ( !sieve_opr_string_read(renv, address, &from) ) {
					sieve_runtime_trace_error(renv, "invalid from operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
				break;
			case OPT_MESSAGE:
				if ( !sieve_opr_string_read(renv, address, &message) ) {
					sieve_runtime_trace_error(renv, "invalid from operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
				break;
			case OPT_OPTIONS:
				if ( (options=sieve_opr_stringlist_read(renv, address)) == NULL ) {
					sieve_runtime_trace_error(renv, "invalid options operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
				break;
			default:
				sieve_runtime_trace_error(renv, "unknown optional operand: %d", 
					opt_code);
				return SIEVE_EXEC_BIN_CORRUPT;
			}
		}
	}
	
	/* Reason operand */
	if ( !sieve_opr_string_read(renv, address, &method_uri) ) {
		sieve_runtime_trace_error(renv, "invalid method operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
		
	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, "NOTIFY action");	

	/* Check operands */

	if ( (result=ext_enotify_runtime_check_operands
		(renv, source_line, method_uri, message, from, options, &method, 
			&method_context)) ) 
	{
		/* Add notify action to the result */

		pool = sieve_result_pool(renv->result);
		act = p_new(pool, struct sieve_enotify_action, 1);
		act->method = method;
		act->method_context = method_context;
		act->importance = importance;
		if ( message != NULL )
			act->message = p_strdup(pool, str_c(message));
		if ( from != NULL )
			act->from = p_strdup(pool, str_c(from));
		
		return ( sieve_result_add_action
			(renv, &act_notify, slist, source_line, (void *) act, 0) >= 0 );
	}
	
	return result;
}

/*
 * Action
 */

/* Runtime verification */

static int act_notify_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED, 
	const struct sieve_action_data *act ATTR_UNUSED,
	const struct sieve_action_data *act_other ATTR_UNUSED)
{
	/* No problems yet */
	return 0;
}

/* Result printing */
 
static void act_notify_print
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_result_print_env *rpenv, void *context, 
	bool *keep ATTR_UNUSED)	
{
	const struct sieve_enotify_action *act = 
		(const struct sieve_enotify_action *) context;

	sieve_result_action_printf
		( rpenv, "send notification with method '%s:':", act->method->identifier);
		
	if ( act->method->action_print != NULL )
		act->method->action_print(rpenv, act);
}

/* Result execution */

static bool act_notify_commit
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context, 
	bool *keep ATTR_UNUSED)
{
	const struct sieve_enotify_action *act = 
		(const struct sieve_enotify_action *) tr_context;
	struct sieve_enotify_exec_env nenv;
	struct sieve_enotify_log nlog;
		
	memset(&nlog, 0, sizeof(nlog));
	nlog.location = sieve_action_get_location(aenv);
	nlog.ehandler = sieve_result_get_error_handler(aenv->result);

	nenv.scriptenv = aenv->scriptenv;
	nenv.msgdata = aenv->msgdata;
	nenv.notify_log = &nlog;

	if ( act->method->action_execute != NULL )
		return act->method->action_execute(&nenv, act);
			
	return TRUE;
}




