/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "istream.h"
#include "istream-header-filter.h"

#include "rfc2822.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-address.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"
#include "sieve-result.h"

#include <stdio.h>

/* 
 * Configuration 
 */

#define CMD_REDIRECT_DUPLICATE_KEEP (3600 * 24)

/* 
 * Redirect command 
 * 
 * Syntax
 *   redirect <address: string>
 */

static bool cmd_redirect_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_redirect_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command cmd_redirect = { 
	"redirect", 
	SCT_COMMAND,
	1, 0, FALSE, FALSE, 
	NULL, NULL,
	cmd_redirect_validate, 
	cmd_redirect_generate, 
	NULL 
};

/* 
 * Redirect operation 
 */

static bool cmd_redirect_operation_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_redirect_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation cmd_redirect_operation = { 
	"REDIRECT",
	NULL, 
	SIEVE_OPERATION_REDIRECT,
	cmd_redirect_operation_dump, 
	cmd_redirect_operation_execute 
};

/* 
 * Redirect action 
 */

static int act_redirect_check_duplicate
	(const struct sieve_runtime_env *renv,
		const struct sieve_action_data *act, 
		const struct sieve_action_data *act_other);
static void act_redirect_print
	(const struct sieve_action *action, const struct sieve_result_print_env *rpenv,
		void *context, bool *keep);	
static bool act_redirect_commit
	(const struct sieve_action *action, const struct sieve_action_exec_env *aenv,
		void *tr_context, bool *keep);
		
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

struct act_redirect_context {
	const char *to_address;
};

/* 
 * Validation 
 */

static bool cmd_redirect_validate
(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	/* Check and activate address argument */

	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "address", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(validator, cmd, arg, FALSE) )
		return FALSE;

	/* We can only assess the validity of the outgoing address when it is 
	 * a string literal. For runtime-generated strings this needs to be 
	 * done at runtime (FIXME!)
     */
	if ( sieve_argument_is_string_literal(arg) ) {
		string_t *address = sieve_ast_argument_str(arg);
		const char *error;
		const char *norm_address;

		T_BEGIN {
			/* Verify and normalize the address to 'local_part@domain' */
			norm_address = sieve_address_normalize(address, &error);
		
			if ( norm_address == NULL ) {
				sieve_argument_validate_error(validator, arg, 
					"specified redirect address '%s' is invalid: %s",
					str_sanitize(str_c(address),128), error);
			} else {
				/* Replace string literal in AST */
				sieve_ast_argument_string_setc(arg, norm_address);
			}
		} T_END;

		return ( norm_address != NULL );
	}		

	return TRUE;
}

/*
 * Code generation
 */
 
static bool cmd_redirect_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	sieve_operation_emit_code(cgenv->sbin, &cmd_redirect_operation);

	/* Emit line number */
	sieve_code_source_line_emit(cgenv->sbin, sieve_command_source_line(ctx));

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, ctx, NULL);
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

	/* Source line */
    if ( !sieve_code_source_line_dump(denv, address) )
        return FALSE;

	if ( !sieve_code_dumper_print_optional_operands(denv, address) )
		return FALSE;

	return sieve_opr_string_dump(denv, address, "reason");
}

/*
 * Intepretation
 */

static int cmd_redirect_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_side_effects_list *slist = NULL;
	struct act_redirect_context *act;
	string_t *redirect;
	unsigned int source_line;
	pool_t pool;
	int ret = 0;

	/* Source line */
    if ( !sieve_code_source_line_read(renv, address, &source_line) ) {
		sieve_runtime_trace_error(renv, "invalid source line");
        return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Optional operands (side effects) */
	if ( (ret=sieve_interpreter_handle_optional_operands
		(renv, address, &slist)) <= 0 )
		return ret;

	/* Read the address */
	if ( !sieve_opr_string_read(renv, address, &redirect) ) {
		sieve_runtime_trace_error(renv, "invalid address string");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* FIXME: perform address normalization if the string is not a string literal
	 */

	sieve_runtime_trace(renv, "REDIRECT action (\"%s\")", str_sanitize(str_c(redirect), 64));
	
	/* Add redirect action to the result */

	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_redirect_context, 1);
	act->to_address = p_strdup(pool, str_c(redirect));
	
	ret = sieve_result_add_action
		(renv, &act_redirect, slist, source_line, (void *) act, sieve_max_redirects);
	
	return ( ret >= 0 );
}

/*
 * Action implementation
 */
 
static int act_redirect_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action_data *act, 
	const struct sieve_action_data *act_other)
{
	struct act_redirect_context *ctx1 = 
		(struct act_redirect_context *) act->context;
	struct act_redirect_context *ctx2 = 
		(struct act_redirect_context *) act_other->context;
	
	/* Address is already normalized, strcmp suffices to assess duplicates */
	if ( strcmp(ctx1->to_address, ctx2->to_address) == 0 ) 
		return 1;
		
	return 0;
}

static void act_redirect_print
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_result_print_env *rpenv, void *context, bool *keep)	
{
	struct act_redirect_context *ctx = (struct act_redirect_context *) context;
	
	sieve_result_action_printf(rpenv, "redirect message to: %s", 
		str_sanitize(ctx->to_address, 128));
	
	*keep = FALSE;
}

static bool act_redirect_send	
(const struct sieve_action_exec_env *aenv, struct act_redirect_context *ctx)
{
	static const char *hide_headers[] = { "Return-Path", "X-Sieve" };

	const struct sieve_message_data *msgdata = aenv->msgdata;
	const struct sieve_script_env *senv = aenv->scriptenv;
	struct istream *input;
	void *smtp_handle;
	FILE *f;
	const unsigned char *data;
	size_t size;
	int ret;
	
	/* Just to be sure */
	if ( senv->smtp_open == NULL || senv->smtp_close == NULL ) {
		sieve_result_warning(aenv, "redirect action has no means to send mail.");
		return TRUE;
	}
	
	if (mail_get_stream(msgdata->mail, NULL, NULL, &input) < 0)
		return FALSE;
		
	/* Open SMTP transport */
	smtp_handle = senv->smtp_open(ctx->to_address, msgdata->return_path, &f);

	/* Remove unwanted headers */
	input = i_stream_create_header_filter
		(input, HEADER_FILTER_EXCLUDE | HEADER_FILTER_NO_CR, hide_headers,
			N_ELEMENTS(hide_headers), null_header_filter_callback, NULL);

	/* Prepend sieve version header (should not affect signatures) */
	rfc2822_header_field_write(f, "X-Sieve", SIEVE_IMPLEMENTATION);

	/* Pipe the message to the outgoing SMTP transport */
	while ((ret = i_stream_read_data(input, &data, &size, 0)) > 0) {	
		if (fwrite(data, size, 1, f) == 0)
			break;
		i_stream_skip(input, size);
	}
	i_stream_unref(&input);

	/* Close SMTP transport */
	if ( !senv->smtp_close(smtp_handle) ) {
		sieve_result_error(aenv, 
			"failed to redirect message to <%s> "
			"(refer to server log for more information)",
			str_sanitize(ctx->to_address, 80));
		return FALSE;
	}
	
	return TRUE;
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
				str_sanitize(ctx->to_address, 128));
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
			str_sanitize(ctx->to_address, 128));	

		/* Indicate that message was successfully forwarded */
		aenv->exec_status->message_forwarded = TRUE;

		/* Cancel implicit keep */
		*keep = FALSE;

		return TRUE;
	}
  
	return FALSE;
}


