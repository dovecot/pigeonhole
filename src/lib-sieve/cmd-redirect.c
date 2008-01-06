#include "lib.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "istream.h"
#include "istream-header-filter.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"
#include "sieve-result.h"

#include <stdio.h>

/* Config */

#define CMD_REDIRECT_DUPLICATE_KEEP (3600 * 24)

/* Forward declarations */

static bool cmd_redirect_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool cmd_redirect_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_redirect_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_redirect_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Redirect command 
 * 
 * Syntax
 *   redirect <address: string>
 */

const struct sieve_command cmd_redirect = { 
	"redirect", 
	SCT_COMMAND,
	1, 0, FALSE, FALSE, 
	NULL, NULL,
	cmd_redirect_validate, 
	cmd_redirect_generate, 
	NULL 
};

/* Redirect operation */

const struct sieve_operation cmd_redirect_operation = { 
	"REDIRECT",
	NULL, 
	SIEVE_OPERATION_REDIRECT,
	cmd_redirect_operation_dump, 
	cmd_redirect_operation_execute 
};

/* Redirect action */

static int act_redirect_check_duplicate
	(const struct sieve_runtime_env *renv,
		const struct sieve_action *action1, void *context1, void *context2);
static void act_redirect_print
	(const struct sieve_action *action, void *context, bool *keep);	
static bool act_redirect_commit
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep);
		
struct act_redirect_context {
	const char *to_address;
};

const struct sieve_action act_redirect = {
	"redirect",
	SIEVE_ACTFLAG_TRIES_DELIVER,
	act_redirect_check_duplicate, 
	NULL,
	act_redirect_print,
	NULL, NULL,
	act_redirect_commit,
	NULL
};

/* Validation */

static bool cmd_redirect_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	/* Check argument */
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "address", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(validator, cmd, arg, FALSE);
}

/*
 * Generation
 */
 
static bool cmd_redirect_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_operation(generator, &cmd_redirect_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
	
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_redirect_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "REDIRECT");
	sieve_code_descend(denv);

	if ( !sieve_code_dumper_print_optional_operands(denv, address) )
		return FALSE;

	return 
		sieve_opr_string_dump(denv, address);
}

/*
 * Intepretation
 */

static bool cmd_redirect_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_side_effects_list *slist = NULL;
	struct act_redirect_context *act;
	string_t *redirect;
	pool_t pool;
	int ret = 0;

	if ( !sieve_interpreter_handle_optional_operands(renv, address, &slist) )
		return FALSE;

	t_push();

	if ( !sieve_opr_string_read(renv, address, &redirect) ) {
		t_pop();
		return FALSE;
	}

	printf(">> REDIRECT \"%s\"\n", str_c(redirect));
	
	/* Add redirect action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_redirect_context, 1);
	act->to_address = p_strdup(pool, str_c(redirect));
	
	ret = sieve_result_add_action(renv, &act_redirect, slist, (void *) act);
	
	t_pop();
	return (ret >= 0);
}

/*
 * Action
 */
 
static int act_redirect_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action *action1 ATTR_UNUSED, 
	void *context1, void *context2)
{
	struct act_redirect_context *ctx1 = (struct act_redirect_context *) context1;
	struct act_redirect_context *ctx2 = (struct act_redirect_context *) context2;
	
	if ( strcmp(ctx1->to_address, ctx2->to_address) == 0 ) 
		return 1;
		
	return 0;
}

static void act_redirect_print
(const struct sieve_action *action ATTR_UNUSED, void *context, bool *keep)	
{
	struct act_redirect_context *ctx = (struct act_redirect_context *) context;
	
	printf("* redirect message to: %s\n", ctx->to_address);
	
	*keep = FALSE;
}

static bool act_redirect_send	
	(const struct sieve_action_exec_env *aenv, struct act_redirect_context *ctx)
{
	const struct sieve_message_data *msgdata = aenv->msgdata;
	const struct sieve_script_env *senv = aenv->scriptenv;
	struct istream *input;
	static const char *hide_headers[] = { "Return-Path" };
	void *smtp_handle;
	FILE *f;
	const unsigned char *data;
	size_t size;
	int ret;
	
	/* Just to be sure */
	if ( senv->smtp_open == NULL || senv->smtp_close == NULL ) {
		sieve_result_error(aenv, "redirect action has no means to send mail.");
		return FALSE;
	}
	
	if (mail_get_stream(msgdata->mail, NULL, NULL, &input) < 0)
		return -1;
		
  smtp_handle = senv->smtp_open(ctx->to_address, msgdata->return_path, &f);

  input = i_stream_create_header_filter
  	(input, HEADER_FILTER_EXCLUDE | HEADER_FILTER_NO_CR, hide_headers,
		N_ELEMENTS(hide_headers), null_header_filter_callback, NULL);

	while ((ret = i_stream_read_data(input, &data, &size, 0)) > 0) {	
		if (fwrite(data, size, 1, f) == 0)
			break;
		i_stream_skip(input, size);
	}

	return senv->smtp_close(smtp_handle);
}

static bool act_redirect_commit
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep)
{
	struct act_redirect_context *ctx = (struct act_redirect_context *) tr_context;
	const struct sieve_message_data *msgdata = aenv->msgdata;
	const struct sieve_script_env *senv = aenv->scriptenv;
	const char *dupeid;
	
	/* Prevent mail loops if possible */
	dupeid = msgdata->id == NULL ? 
  		NULL : t_strdup_printf("%s-%s", msgdata->id, ctx->to_address);
	if (dupeid != NULL) {
		/* Check whether we've seen this message before */
  		if (senv->duplicate_check(dupeid, strlen(dupeid), senv->username)) {
			sieve_result_log(aenv, "discarded duplicate forward to <%s>",
				str_sanitize(ctx->to_address, 80));
			return TRUE;
		}
	}
	
	/* Try to forward the message */
	if ( act_redirect_send(aenv, ctx) ) {
	
		/* Mark this message id as forwarded to the specified destination */
		if (dupeid != NULL) {
			senv->duplicate_mark(dupeid, strlen(dupeid), senv->username,
				ioloop_time + CMD_REDIRECT_DUPLICATE_KEEP);
		}
	
		sieve_result_log(aenv, "forwarded to <%s>", 
			str_sanitize(ctx->to_address, 80));	

		*keep = FALSE;
  		return TRUE;
  	}
  
	return FALSE;
}


