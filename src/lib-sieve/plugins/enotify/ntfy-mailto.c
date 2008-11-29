/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */
 
#include "lib.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "message-date.h"

#include "sieve-common.h"
#include "sieve-actions.h"
#include "sieve-result.h"

#include "sieve-ext-enotify.h"

/* 
 * Mailto notification method
 */
static bool ntfy_mailto_validate_uri
	(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
		const char *uri);

const struct sieve_enotify_method mailto_notify = {
	"mailto",
	ntfy_mailto_validate_uri,
	NULL
};

/*
 * 
 */

static bool ntfy_mailto_parse_uri
(const char *uri, unsigned int len, const char **recipient_r, 
	const char ***headers)
{
	*recipient_r = "stephan@rename-it.nl";
	*headers = NULL;

	/* Scheme already parsed, starting parse after colon */

	/* First parse e-mail address */
}

/*
 * Validation
 */

static bool ntfy_mailto_validate_uri
(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
	const char *uri)
{
	return TRUE;
}

/*
 * Execution
 */

static bool _contains_8bit(const char *msg)
{
	const unsigned char *s = (const unsigned char *)msg;

	for (; *s != '\0'; s++) {
		if ((*s & 0x80) != 0)
			return TRUE;
	}
	
	return FALSE;
}
 
static bool ntfy_mailto_execute
(const struct sieve_action_exec_env *aenv, 
	const struct sieve_enotify_context *nctx)
{ 
	const struct sieve_message_data *msgdata = aenv->msgdata;
	const struct sieve_script_env *senv = aenv->scriptenv;
	void *smtp_handle;
	FILE *f;
	const char *outmsgid;
	const char *recipient = "BOGUS";
	
	/* Just to be sure */
	if ( senv->smtp_open == NULL || senv->smtp_close == NULL ) {
		sieve_result_warning(aenv, 
			"notify mailto method has no means to send mail.");
		return FALSE;
	}
	
	smtp_handle = senv->smtp_open(msgdata->return_path, NULL, &f);
	outmsgid = sieve_get_new_message_id(senv);
	
	fprintf(f, "Message-ID: %s\r\n", outmsgid);
	fprintf(f, "Date: %s\r\n", message_date_create(ioloop_time));
	fprintf(f, "X-Sieve: %s\r\n", SIEVE_IMPLEMENTATION);
	
	switch ( nctx->importance ) {
	case 1:
		fprintf(f, "X-Priority: 1 (Highest)\r\n");
		fprintf(f, "Importance: High\r\n");
		break;
	case 3:
    fprintf(f, "X-Priority: 5 (Lowest)\r\n");
    fprintf(f, "Importance: Low\r\n");
    break;
	case 2:
	default:
		fprintf(f, "X-Priority: 3 (Normal)\r\n");
		fprintf(f, "Importance: Normal\r\n");
		break;
	}
	 
	fprintf(f, "From: Postmaster <%s>\r\n", senv->postmaster_address);
	fprintf(f, "To: <%s>\r\n", recipient);
	fprintf(f, "Subject: [SIEVE] New mail notification\r\n");
	fprintf(f, "Auto-Submitted: auto-generated (notify)\r\n");
	fprintf(f, "Precedence: bulk\r\n");
	
	if (_contains_8bit(nctx->message)) {
			fprintf(f, "MIME-Version: 1.0\r\n");
	    fprintf(f, "Content-Type: text/plain; charset=UTF-8\r\n");
	    fprintf(f, "Content-Transfer-Encoding: 8bit\r\n");
	}
	fprintf(f, "\r\n");
	fprintf(f, "%s\r\n", nctx->message);
	
	if ( senv->smtp_close(smtp_handle) ) {
		sieve_result_log(aenv, 
			"sent mail notification to <%s>", str_sanitize(recipient, 80));
	} else {
		sieve_result_error(aenv,
			"failed to send mail notification to <%s> ",
			"(refer to system log for more information)", 
			str_sanitize(recipient, 80));
	}

	return TRUE;
}
