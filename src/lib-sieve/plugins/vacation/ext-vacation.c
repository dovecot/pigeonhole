/* Extension vacation
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-vacation-07
 * Implementation: almost complete, the required sopport for  Refences header 
 *   is missing and :addresses and :mime are ignored.
 * Status: experimental, largely untested
 * 
 */

#include "lib.h"
#include "md5.h"
#include "hostpid.h"
#include "str-sanitize.h"
#include "message-address.h"
#include "message-date.h"
#include "ioloop.h"

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"
#include "sieve-result.h"

#include <stdio.h>

/* Forward declarations */

static bool ext_vacation_load(int ext_id);
static bool ext_vacation_validator_load(struct sieve_validator *validator);

static bool ext_vacation_opcode_dump
	(const struct sieve_opcode *opcode,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool ext_vacation_opcode_execute
	(const struct sieve_opcode *opcode, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_vacation_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool cmd_vacation_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_vacation_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Extension definitions */

int ext_my_id;

const struct sieve_opcode vacation_opcode;

const struct sieve_extension vacation_extension = { 
	"vacation", 
	ext_vacation_load,
	ext_vacation_validator_load, 
	NULL, NULL, NULL, 
	SIEVE_EXT_DEFINE_OPCODE(vacation_opcode),
	NULL
};

static bool ext_vacation_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Vacation command 
 *	
 * Syntax: 
 *    vacation [":days" number] [":subject" string]
 *                 [":from" string] [":addresses" string-list]
 *                 [":mime"] [":handle" string] <reason: string>
 */
static const struct sieve_command vacation_command = { 
	"vacation",
	SCT_COMMAND, 
	1, 0, FALSE, FALSE, 
	cmd_vacation_registered,
	NULL,  
	cmd_vacation_validate, 
	cmd_vacation_generate, 
	NULL 
};

/* Vacation opcode */
const struct sieve_opcode vacation_opcode = { 
	"VACATION",
	SIEVE_OPCODE_CUSTOM,
	&vacation_extension,
	0,
	ext_vacation_opcode_dump, 
	ext_vacation_opcode_execute
};

/* Vacation action */

static int act_vacation_check_duplicate
	(const struct sieve_runtime_env *renv, const struct sieve_action *action1,
    	void *context1, void *context2);
int act_vacation_check_conflict
	(const struct sieve_runtime_env *renv, const struct sieve_action *action,
		const struct sieve_action *other_action, void *context);
static void act_vacation_print
	(const struct sieve_action *action, void *context, bool *keep);	
static bool act_vacation_commit
	(const struct sieve_action *action,	const struct sieve_action_exec_env *aenv, 
		void *tr_context, bool *keep);
		
struct act_vacation_context {
	const char *reason;

	sieve_size_t days;
	const char *subject;
	const char *handle;
	bool mime;
	const char *from;
	const char *const *addresses;
};

const struct sieve_action act_vacation = {
	"vacation",
	SIEVE_ACTFLAG_SENDS_RESPONSE,
	act_vacation_check_duplicate, 
	act_vacation_check_conflict,
	act_vacation_print,
	NULL, NULL,
	act_vacation_commit,
	NULL
};

/* Tag validation */

static bool cmd_vacation_validate_number_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	
	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);
	
	/* Check syntax:
	 *   :days number
	 */
	if ( !sieve_validate_tag_parameter
		(validator, cmd, tag, *arg, SAAT_NUMBER) ) {
		return FALSE;
	}

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);
	
	return TRUE;
}

static bool cmd_vacation_validate_string_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);
	
	/* Check syntax:
	 *   :subject string
	 *   :from string
	 *   :handle string
	 */
	if ( !sieve_validate_tag_parameter
		(validator, cmd, tag, *arg, SAAT_STRING) ) {
		return FALSE;
	}
		
	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool cmd_vacation_validate_stringlist_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);
	
	/* Check syntax:
	 *   :addresses string-list
	 */
	if ( !sieve_validate_tag_parameter
		(validator, cmd, tag, *arg, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool cmd_vacation_validate_mime_tag
	(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_ast_argument **arg ATTR_UNUSED, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	/* FIXME: currently not generated */
	*arg = sieve_ast_arguments_detach(*arg,1);
		
	return TRUE;
}

/* Command registration */

static const struct sieve_argument vacation_days_tag = { 
	"days", NULL, 
	cmd_vacation_validate_number_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_subject_tag = { 
	"subject", NULL, 
	cmd_vacation_validate_string_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_from_tag = { 
	"from", NULL, 
	cmd_vacation_validate_string_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_addresses_tag = { 
	"addresses", NULL, 
	cmd_vacation_validate_stringlist_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_mime_tag = { 
	"mime",	NULL, 
	cmd_vacation_validate_mime_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_handle_tag = { 
	"handle", NULL, 
	cmd_vacation_validate_string_tag, 
	NULL, NULL 
};

enum cmd_vacation_optional {
	OPT_END,
	OPT_DAYS,
	OPT_SUBJECT,
	OPT_FROM,
	OPT_ADDRESSES,
	OPT_MIME,
	OPT_HANDLE
};

static bool cmd_vacation_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_days_tag, OPT_DAYS); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_subject_tag, OPT_SUBJECT); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_from_tag, OPT_FROM); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_addresses_tag, OPT_ADDRESSES); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_mime_tag, OPT_MIME); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_handle_tag, OPT_HANDLE); 	

	return TRUE;
}

/* Command validation */

static bool cmd_vacation_validate(struct sieve_validator *validator, 
	struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg = cmd->first_positional;

	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "reason", 1, SAAT_STRING) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, arg);	
	
	return TRUE;
}

/* Load extension into validator */
static bool ext_vacation_validator_load(struct sieve_validator *validator)
{
	/* Register new command */
	sieve_validator_register_command(validator, &vacation_command);

	return TRUE;
}

/*
 * Generation
 */
 
static bool cmd_vacation_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_opcode_ext(generator, &vacation_opcode, ext_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool ext_vacation_opcode_dump
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{	
	int opt_code = 1;
	
	sieve_code_dumpf(denv, "VACATION");
	sieve_code_descend(denv);	

	if ( sieve_operand_optional_present(denv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(denv->sbin, address, &opt_code) ) 
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_DAYS:
				if ( !sieve_opr_number_dump(denv, address) )
					return FALSE;
				break;
			case OPT_SUBJECT:
			case OPT_FROM:
			case OPT_HANDLE: 
				if ( !sieve_opr_string_dump(denv, address) )
					return FALSE;
				break;
			case OPT_ADDRESSES:
				if ( !sieve_opr_stringlist_dump(denv, address) )
					return FALSE;
				break;
			case OPT_MIME:
				break;
			
			default:
				return FALSE;
			}
		}
	}
	
	return sieve_opr_string_dump(denv, address);
}

/* 
 * Code execution
 */
 
static bool ext_vacation_opcode_execute
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	struct sieve_side_effects_list *slist = NULL;
	struct act_vacation_context *act;
	pool_t pool;
	int opt_code = 1;
	sieve_size_t days = 7;
	string_t *reason, *subject = NULL, *from = NULL, *handle = NULL; 
		
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) ) 
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_DAYS:
				if ( !sieve_opr_number_read(renv->sbin, address, &days) ) return FALSE;
				break;
			case OPT_SUBJECT:
				if ( !sieve_opr_string_read(renv->sbin, address, &subject) ) return FALSE;
				break;
			case OPT_FROM:
				if ( !sieve_opr_string_read(renv->sbin, address, &from) ) return FALSE;
				break;
			case OPT_HANDLE: 
				if ( !sieve_opr_string_read(renv->sbin, address, &handle) ) return FALSE;
				break;
			case OPT_ADDRESSES:
				if ( sieve_opr_stringlist_read(renv->sbin, address) == NULL ) return FALSE;
				break;
			case OPT_MIME:
				break;
			default:
				return FALSE;
			}
		}
	}
	
	if ( !sieve_opr_string_read(renv->sbin, address, &reason) ) 
		return FALSE;
	
	printf(">> VACATION \"%s\"\n", str_c(reason));
	
	/* Add vacation action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_vacation_context, 1);
	act->reason = p_strdup(pool, str_c(reason));
	if ( subject != NULL )
		act->subject = p_strdup(pool, str_c(subject));
	if ( from != NULL )
		act->from = p_strdup(pool, str_c(from));
	if ( handle != NULL )
		act->handle = p_strdup(pool, str_c(handle));
	act->days = days;
	
	/* FIXME: :addresses is ignored */
	
	return ( sieve_result_add_action(renv, &act_vacation, slist, (void *) act) >= 0 );
}

/*
 * Action
 */

static int act_vacation_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action *action1 ATTR_UNUSED,
	void *context1 ATTR_UNUSED, void *context2 ATTR_UNUSED)
{
	sieve_runtime_error(renv, "duplicate 'vacation' action not allowed.");
	return -1;
}

int act_vacation_check_conflict
(const struct sieve_runtime_env *renv,
	const struct sieve_action *action ATTR_UNUSED,
	const struct sieve_action *other_action, void *context ATTR_UNUSED)
{
	if ( (other_action->flags & SIEVE_ACTFLAG_SENDS_RESPONSE) > 0 ) {
		sieve_runtime_error(renv, "'vacation' action conflicts with other action: "
			"'%s' action sends a response back to the sender.",	
			other_action->name);
		return -1;
	}

	return 0;
}
 
static void act_vacation_print
(const struct sieve_action *action ATTR_UNUSED, void *context, 
	bool *keep ATTR_UNUSED)	
{
	struct act_vacation_context *ctx = (struct act_vacation_context *) context;
	
	printf( 	"* send vacation message:\n"
						"    => days   : %d\n", ctx->days);
	if ( ctx->subject != NULL )
		printf(	"    => subject: %s\n", ctx->subject);
	if ( ctx->from != NULL )
		printf(	"    => from   : %s\n", ctx->from);
	if ( ctx->handle != NULL )
		printf(	"    => handle : %s\n", ctx->handle);
	printf(		"\nSTART MESSAGE\n%s\nEND MESSAGE\n", ctx->reason);
}

static const char * const _list_headers[] = {
	"list-id",
	"list-owner",
	"list-subscribe",
	"list-post",	
	"list-unsubscribe",
	"list-help",
	"list-archive",
	NULL
};

static const char * const _my_address_headers[] = {
	"to",
	"cc",
	"bcc",
	"resent-to",	
	"resent-cc",
	"resent-bcc",
	NULL
};

static inline bool _is_system_address(const char *address)
{
	if ( strncasecmp(address, "MAILER-DAEMON", 13) == 0 )
		return TRUE;

	if ( strncasecmp(address, "LISTSERV", 8) == 0 )
		return TRUE;

	if ( strncasecmp(address, "majordomo", 9) == 0 )
		return TRUE;

	if ( strstr(address, "-request@") != NULL )
		return TRUE;

	if ( strncmp(address, "owner-", 6) == 0 )
		return TRUE;

	return FALSE;
}

static inline bool _contains_my_address
	(const char * const *headers, const char *my_address)
{
	const char *const *hdsp = headers;
	
	while ( *hdsp != NULL ) {
		const struct message_address *addr;

		t_push();
	
		addr = message_address_parse
			(pool_datastack_create(), (const unsigned char *) *hdsp, 
				strlen(*hdsp), 256, FALSE);

		while ( addr != NULL ) {
			if (addr->domain != NULL) {
				i_assert(addr->mailbox != NULL);

				if ( strcmp(t_strconcat(addr->mailbox, "@", addr->domain, NULL),
					my_address) == 0 ) {
					t_pop();
					return TRUE;
				}
			}

			addr = addr->next;
		}

		t_pop();
		
		hdsp++;
	}
	
	return FALSE;
}

static bool act_vacation_send	
	(const struct sieve_action_exec_env *aenv, struct act_vacation_context *ctx)
{
	const struct sieve_message_data *msgdata = aenv->msgdata;
	const struct sieve_mail_environment *mailenv = aenv->mailenv;
	void *smtp_handle;
  FILE *f;
 	const char *outmsgid;

	/* Just to be sure */
	if ( mailenv->smtp_open == NULL || mailenv->smtp_close == NULL ) {
		sieve_result_error(aenv, "vacation action has no means to send mail.");
		return FALSE;
	}

  smtp_handle = mailenv->smtp_open(msgdata->return_path, NULL, &f);
  outmsgid = sieve_get_new_message_id(mailenv);
    
	fprintf(f, "Message-ID: %s\r\n", outmsgid);
	fprintf(f, "Date: %s\r\n", message_date_create(ioloop_time));
	if ( ctx->from != NULL && *(ctx->from) != '\0' )
		fprintf(f, "From: <%s>\r\n", ctx->from);
	else
		fprintf(f, "From: <%s>\r\n", msgdata->to_address);
		
	fprintf(f, "To: <%s>\r\n", msgdata->return_path);
	fprintf(f, "Subject: %s\r\n", str_sanitize(ctx->subject, 80));
	if ( msgdata->id != NULL ) 
		fprintf(f, "In-Reply-To: %s\r\n", msgdata->id);
	fprintf(f, "Auto-Submitted: auto-replied (vacation)\r\n");

	/* FIXME: What about the required references header ? */

	fprintf(f, "X-Sieve: %s\r\n", SIEVE_IMPLEMENTATION);

	fprintf(f, "Precedence: bulk\r\n");
	fprintf(f, "MIME-Version: 1.0\r\n");
    
	if (	ctx->mime	) {
		fprintf(f, "Content-Type: multipart/mixed;"
			"\r\n\tboundary=\"%s/%s\"\r\n", my_pid, mailenv->hostname);
		fprintf(f, "\r\nThis is a MIME-encapsulated message\r\n\r\n");
		fprintf(f, "--%s/%s\r\n", my_pid, mailenv->hostname);
	} else {
		fprintf(f, "Content-Type: text/plain; charset=utf-8\r\n");
		fprintf(f, "Content-Transfer-Encoding: 8bit\r\n");
		fprintf(f, "\r\n");
	}

	fprintf(f, "%s\r\n", ctx->reason);
    
	if ( ctx->mime )
		fprintf(f, "\r\n--%s/%s--\r\n", my_pid, mailenv->hostname);

	if ( mailenv->smtp_close(smtp_handle) ) {
		/*mailenv->duplicate_mark(outmsgid, strlen(outmsgid),
		  mailenv->username, ioloop_time + DUPLICATE_DEFAULT_KEEP);*/
		return TRUE;
	}
	
	return FALSE;
}

static void act_vacation_hash
(const struct sieve_message_data *msgdata, struct act_vacation_context *vctx, 
	unsigned char hash_r[])
{
	struct md5_context ctx;

	md5_init(&ctx);
	md5_update(&ctx, msgdata->return_path, strlen(msgdata->return_path));

	if ( vctx->handle != NULL && *(vctx->handle) != '\0' ) 
		md5_update(&ctx, vctx->handle, strlen(vctx->handle));
	else {
		md5_update(&ctx, msgdata->to_address, strlen(msgdata->to_address));
		md5_update(&ctx, vctx->reason, strlen(vctx->reason));
	}

	md5_final(&ctx, hash_r);
}

static bool act_vacation_commit
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context, 
	bool *keep ATTR_UNUSED)
{
	const char *const *hdsp;
	const struct sieve_message_data *msgdata = aenv->msgdata;
	const struct sieve_mail_environment *mailenv = aenv->mailenv;
	struct act_vacation_context *ctx = (struct act_vacation_context *) tr_context;
	unsigned char dupl_hash[MD5_RESULTLEN];
	const char *const *headers;
	pool_t pool;

	/* Is the return_path unset ?
	 */
	if ( msgdata->return_path == NULL || *(msgdata->return_path) == '\0' ) {
		sieve_result_log(aenv, "discarded vacation reply to <>");
  	return TRUE;
  }    
	
	/* Are we perhaps trying to respond to ourselves ? 
	 * (FIXME: verify this to :addresses as well)
	 */
	if ( strcmp(msgdata->return_path, msgdata->to_address) == 0 ) {
		sieve_result_log(aenv, "discarded vacation reply to own address");
  	return TRUE;
	}
	
	/* Did whe respond to this user before? */
	act_vacation_hash(msgdata, ctx, dupl_hash);
	if (mailenv->duplicate_check(dupl_hash, sizeof(dupl_hash), mailenv->username)) 
	{
		sieve_result_log(aenv, "discarded duplicate vacation response to <%s>",
			str_sanitize(msgdata->return_path, 80));
		return TRUE;
	}
	
	/* Are we trying to respond to a mailing list ? */
	hdsp = _list_headers;
	while ( *hdsp != NULL ) {
		if ( mail_get_headers_utf8
			(msgdata->mail, *hdsp, &headers) >= 0 && headers[0] != NULL ) {	
			/* Yes, bail out */
			sieve_result_log(aenv, 
				"discarding vacation response to mailinglist recipient <%s>", 
				msgdata->return_path);	
			return TRUE;				 
		}
		hdsp++;
	}
	
	/* Is the message that we are replying to an automatic reply ? */
	if ( mail_get_headers_utf8
		(msgdata->mail, "auto-submitted", &headers) >= 0 ) {
		/* Theoretically multiple headers could exist, so lets make sure */
		hdsp = headers;
		while ( *hdsp != NULL ) {
			if ( strcasecmp(*hdsp, "no") != 0 ) {
				sieve_result_log(aenv, 
					"discardig vacation response to auto-submitted message from <%s>", 
					msgdata->return_path);	
					return TRUE;				 
			}
			hdsp++;
		}
	}
	
	/* Check for non-standard precedence header */
	if ( mail_get_headers_utf8
		(msgdata->mail, "precedence", &headers) >= 0 ) {
		/* Theoretically multiple headers could exist, so lets make sure */
		hdsp = headers;
		while ( *hdsp != NULL ) {
			if ( strcasecmp(*hdsp, "junk") == 0 || strcasecmp(*hdsp, "bulk") == 0 ||
				strcasecmp(*hdsp, "list") == 0 ) {
				sieve_result_log(aenv, 
					"discarding vacation response to precedence=%s message from <%s>", 
					*hdsp, msgdata->return_path);	
					return TRUE;				 
			}
			hdsp++;
		}
	}
	
	/* Do not reply to system addresses */
	if ( _is_system_address(msgdata->return_path) ) {
		sieve_result_log(aenv, 
			"not sending vacation response to system address <%s>", 
			msgdata->return_path);	
		return TRUE;				
	} 
	
	/* Is the original message directly addressed to me? */
	hdsp = _my_address_headers;
	while ( *hdsp != NULL ) {
		if ( mail_get_headers_utf8
			(msgdata->mail, *hdsp, &headers) >= 0 && headers[0] != NULL ) {	
			
			if ( _contains_my_address(headers, msgdata->to_address) ) 
				break;
		}
		hdsp++;
	}	

	if ( *hdsp == NULL ) {
		/* No, bail out */
		sieve_result_log(aenv, 
			"discarding vacation response for implicitly delivered message");	
		return TRUE;				 
	}	
		
	/* Make sure we have a subject for our reply */
	if ( ctx->subject == NULL || *(ctx->subject) == '\0' ) {
		if ( mail_get_headers_utf8
			(msgdata->mail, "subject", &headers) >= 0 && headers[0] != NULL ) {
			pool = sieve_result_pool(aenv->result);
			ctx->subject = p_strconcat(pool, "Auto: ", headers[0], NULL);
		}	else {
			ctx->subject = "Automated reply";
		}
	}	
	
	/* Send the message */
	
	if ( act_vacation_send(aenv, ctx) ) {
		sieve_result_log(aenv, "sent vacation response to <%s>", 
			str_sanitize(msgdata->return_path, 80));	

		mailenv->duplicate_mark(dupl_hash, sizeof(dupl_hash), mailenv->username,
			ioloop_time + ctx->days * (24 * 60 * 60));

  	return TRUE;
  }

	sieve_result_error(aenv, "failed to send vacation response to <%s>", 
		str_sanitize(msgdata->return_path, 80));	
	return FALSE;
}




