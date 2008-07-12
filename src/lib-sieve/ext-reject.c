/* Extension reject 
 * ----------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC5228, draft-ietf-sieve-refuse-reject-04
 * Implementation: full  
 * Status: experimental, largely untested
 *
 */

#include "lib.h"
#include "ioloop.h"
#include "hostpid.h"
#include "message-date.h"
#include "message-size.h"
#include "istream.h"
#include "istream-header-filter.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"

/* Forward declarations */

static bool ext_reject_load(int ext_id);
static bool ext_reject_validator_load(struct sieve_validator *validator);

static bool cmd_reject_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_reject_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx); 

static bool ext_reject_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool ext_reject_operation_execute
	(const struct sieve_operation *op,
		const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Extension definitions */

static int ext_my_id;

struct sieve_operation reject_operation;
	
struct sieve_extension reject_extension = { 
	"reject", 
	ext_reject_load,
	ext_reject_validator_load, 
	NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_OPERATION(reject_operation), 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_reject_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* Reject command
 * 
 * Syntax: 
 *   reject <reason: string>
 */
static const struct sieve_command reject_command = { 
	"reject", 
	SCT_COMMAND, 
	1, 0, FALSE, FALSE,
	NULL, NULL,
	cmd_reject_validate, 
	cmd_reject_generate, 
	NULL 
};

/* Reject operation */

struct sieve_operation reject_operation = { 
	"REJECT",
	&reject_extension, 
	0,
	ext_reject_operation_dump, 
	ext_reject_operation_execute 
};

/* Reject action */

static int act_reject_check_duplicate
	(const struct sieve_runtime_env *renv, const struct sieve_action *action1,
		void *context1, void *context2);
int act_reject_check_conflict
	(const struct sieve_runtime_env *renv, const struct sieve_action *action,
    	const struct sieve_action *other_action, void *context);
static void act_reject_print
	(const struct sieve_action *action, void *context, bool *keep);	
static bool act_reject_commit
	(const struct sieve_action *action ATTR_UNUSED, 
		const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep);
		
struct act_reject_context {
	const char *reason;
};

const struct sieve_action act_reject = {
	"reject",
	SIEVE_ACTFLAG_SENDS_RESPONSE,
	act_reject_check_duplicate, 
	act_reject_check_conflict,
	act_reject_print,
	NULL, NULL,
	act_reject_commit,
	NULL
};

/* 
 * Validation 
 */

static bool cmd_reject_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg = cmd->first_positional;
		
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "reason", 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	return sieve_validator_argument_activate(validator, cmd, arg, FALSE);
}

/* Load extension into validator */
static bool ext_reject_validator_load(struct sieve_validator *validator)
{
	/* Register new command */
	sieve_validator_register_command(validator, &reject_command);

	return TRUE;
}

/*
 * Generation
 */
 
static bool cmd_reject_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_operation_ext
		(generator, &reject_operation, ext_my_id);

	/* Generate arguments */
    if ( !sieve_generate_arguments(generator, ctx, NULL) )
        return FALSE;
	
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool ext_reject_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "REJECT");
	sieve_code_descend(denv);
	
	if ( !sieve_code_dumper_print_optional_operands(denv, address) )
        return FALSE;
	
	return
		sieve_opr_string_dump(denv, address);
}

/*
 * Execution
 */

static bool ext_reject_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_side_effects_list *slist = NULL;
	struct act_reject_context *act;
	string_t *reason;
	pool_t pool;
	int ret;

	t_push();
	
	if ( !sieve_interpreter_handle_optional_operands(renv, address, &slist) ) {
		t_pop();
		return FALSE;
	}

	if ( !sieve_opr_string_read(renv, address, &reason) ) {
		t_pop();
		return FALSE;
	}

	sieve_runtime_trace(renv, "REJECT action (\"%s\")", str_c(reason));

	/* Add reject action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_reject_context, 1);
	act->reason = p_strdup(pool, str_c(reason));
	
	ret = sieve_result_add_action(renv, &act_reject, slist, (void *) act);
	
	t_pop();
	return ( ret >= 0 );
}

/*
 * Action
 */

static int act_reject_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action *action1 ATTR_UNUSED,
	void *context1 ATTR_UNUSED, void *context2 ATTR_UNUSED)
{
	sieve_runtime_error(renv, "duplicate 'reject' action not allowed.");	
	return -1;
}
 
int act_reject_check_conflict
(const struct sieve_runtime_env *renv,
	const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action *other_action, void *context ATTR_UNUSED)
{
	if ( (other_action->flags & SIEVE_ACTFLAG_TRIES_DELIVER) > 0 ) {
		sieve_runtime_error(renv, "'reject' action conflicts with other action: "
			"'%s' action tries to deliver the message.",
			other_action->name);	
		return -1;
	}

	if ( (other_action->flags & SIEVE_ACTFLAG_SENDS_RESPONSE) > 0 ) {
		sieve_runtime_error(renv, "'reject' action conflicts with other action: "
			"'%s' sends a response to the sender.",
			other_action->name);	
		return -1;
	}
	
	return 0;
}
 
static void act_reject_print
(const struct sieve_action *action ATTR_UNUSED, void *context, bool *keep)	
{
	struct act_reject_context *ctx = (struct act_reject_context *) context;
	
	printf("* reject message with reason: %s\n", ctx->reason);
	
	*keep = FALSE;
}

static bool act_reject_send	
	(const struct sieve_action_exec_env *aenv, struct act_reject_context *ctx)
{
	const struct sieve_message_data *msgdata = aenv->msgdata;
	const struct sieve_script_env *senv = aenv->scriptenv;
	struct istream *input;
	void *smtp_handle;
	struct message_size hdr_size;
	FILE *f;
	const char *new_msgid, *boundary;
	const unsigned char *data;
	const char *header;
	size_t size;
	int ret;

	/* Just to be sure */
	if ( senv->smtp_open == NULL || senv->smtp_close == NULL ) {
		sieve_result_error(aenv, "reject action has no means to send mail.");
		return FALSE;
	}

	smtp_handle = senv->smtp_open(msgdata->return_path, NULL, &f);

	new_msgid = sieve_get_new_message_id(senv);
	boundary = t_strdup_printf("%s/%s", my_pid, senv->hostname);

	fprintf(f, "Message-ID: %s\r\n", new_msgid);
	fprintf(f, "Date: %s\r\n", message_date_create(ioloop_time));
	fprintf(f, "From: Mail Delivery Subsystem <%s>\r\n",
		senv->postmaster_address);
	fprintf(f, "To: <%s>\r\n", msgdata->return_path);
	fprintf(f, "MIME-Version: 1.0\r\n");
	fprintf(f, "Content-Type: "
		"multipart/report; report-type=disposition-notification;\r\n"
		"\tboundary=\"%s\"\r\n", boundary);
	fprintf(f, "Subject: Automatically rejected mail\r\n");
	fprintf(f, "Auto-Submitted: auto-replied (rejected)\r\n");
	fprintf(f, "Precedence: bulk\r\n");
	fprintf(f, "\r\nThis is a MIME-encapsulated message\r\n\r\n");

	/* Human readable status report */
	fprintf(f, "--%s\r\n", boundary);
	fprintf(f, "Content-Type: text/plain; charset=utf-8\r\n");
	fprintf(f, "Content-Disposition: inline\r\n");
	fprintf(f, "Content-Transfer-Encoding: 8bit\r\n\r\n");

	/* FIXME: var_expand_table expansion not possible */
	fprintf(f, "Your message to <%s> was automatically rejected:\r\n"	
		"%s\r\n", msgdata->to_address, ctx->reason);

	/* MDN status report */
	fprintf(f, "--%s\r\n"
		"Content-Type: message/disposition-notification\r\n\r\n", boundary);
	fprintf(f, "Reporting-UA: %s; Dovecot Mail Delivery Agent\r\n",
		senv->hostname);
	if (mail_get_first_header(msgdata->mail, "Original-Recipient", &header) > 0)
		fprintf(f, "Original-Recipient: rfc822; %s\r\n", header);
	fprintf(f, "Final-Recipient: rfc822; %s\r\n",	msgdata->to_address);

	if ( msgdata->id != NULL )
		fprintf(f, "Original-Message-ID: %s\r\n", msgdata->id);
	fprintf(f, "Disposition: "
		"automatic-action/MDN-sent-automatically; deleted\r\n");
	fprintf(f, "\r\n");

	/* original message's headers */
	fprintf(f, "--%s\r\nContent-Type: message/rfc822\r\n\r\n", boundary);

	if (mail_get_stream(msgdata->mail, &hdr_size, NULL, &input) == 0) {
    /* Note: If you add more headers, they need to be sorted.
       We'll drop Content-Type because we're not including the message
       body, and having a multipart Content-Type may confuse some
       MIME parsers when they don't see the message boundaries. */
    static const char *const exclude_headers[] = {
	    "Content-Type"
    };

    input = i_stream_create_header_filter(input,
    	HEADER_FILTER_EXCLUDE | HEADER_FILTER_NO_CR | HEADER_FILTER_HIDE_BODY, 
    	exclude_headers, N_ELEMENTS(exclude_headers), 
    	null_header_filter_callback, NULL);

		while ((ret = i_stream_read_data(input, &data, &size, 0)) > 0) {
			if (fwrite(data, size, 1, f) == 0)
				break;
				i_stream_skip(input, size);
		}
		i_stream_unref(&input);
			
		i_assert(ret != 0);
	}

	fprintf(f, "\r\n\r\n--%s--\r\n", boundary);

	return senv->smtp_close(smtp_handle);
}

static bool act_reject_commit
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep)
{
	const struct sieve_message_data *msgdata = aenv->msgdata;
	struct act_reject_context *ctx = (struct act_reject_context *) tr_context;
	
	if ( msgdata->return_path == NULL || *(msgdata->return_path) == '\0' ) {
		sieve_result_log(aenv, "discarded reject to <>");
    
		*keep = FALSE;
		return TRUE;
	}
	
	if ( act_reject_send(aenv, ctx) ) {
		sieve_result_log(aenv, "rejected");	

		*keep = FALSE;
		return TRUE;
	}
  
	return FALSE;
}


