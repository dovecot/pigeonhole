/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Notify method mailto
 * --------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5436
 * Implementation: full
 * Status: testing
 *
 */

/* FIXME: URI syntax conforms to something somewhere in between RFC 2368 and
   draft-duerst-mailto-bis-05.txt. Should fully migrate to new specification
   when it matures. This requires modifications to the address parser (no
   whitespace allowed within the address itself) and UTF-8 support will be
   required in the URL.
 */

#include "lib.h"
#include "array.h"
#include "str.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "ostream.h"
#include "message-date.h"
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-address.h"
#include "sieve-address-source.h"
#include "sieve-message.h"
#include "sieve-smtp.h"
#include "sieve-settings.old.h"

#include "sieve-ext-enotify.h"

#include "rfc2822.h"

#include "uri-mailto.h"

/*
 * Configuration
 */

#define NTFY_MAILTO_MAX_RECIPIENTS  8
#define NTFY_MAILTO_MAX_HEADERS     16

/*
 * Mailto notification method
 */

static int
ntfy_mailto_load(const struct sieve_enotify_method *nmth, void **context);
static void
ntfy_mailto_unload(const struct sieve_enotify_method *nmth);

static bool
ntfy_mailto_compile_check_uri(const struct sieve_enotify_env *nenv,
			      const char *uri, const char *uri_body);
static bool
ntfy_mailto_compile_check_from(const struct sieve_enotify_env *nenv,
			       string_t *from);

static const char *
ntfy_mailto_runtime_get_notify_capability(const struct sieve_enotify_env *nenv,
					  const char *uri, const char *uri_body,
					  const char *capability);
static bool
ntfy_mailto_runtime_check_uri(const struct sieve_enotify_env *nenv,
			      const char *uri, const char *uri_body);
static bool
ntfy_mailto_runtime_check_operands(const struct sieve_enotify_env *nenv,
				   const char *uri, const char *uri_body,
				   string_t *message, string_t *from,
				   pool_t context_pool, void **method_context);

static int
ntfy_mailto_action_check_duplicates(
	const struct sieve_enotify_env *nenv,
	const struct sieve_enotify_action *nact,
	const struct sieve_enotify_action *nact_other);

static void
ntfy_mailto_action_print(const struct sieve_enotify_print_env *penv,
			 const struct sieve_enotify_action *nact);

static int
ntfy_mailto_action_execute(const struct sieve_enotify_exec_env *nenv,
			   const struct sieve_enotify_action *nact);

const struct sieve_enotify_method_def mailto_notify = {
	"mailto",
	ntfy_mailto_load,
	ntfy_mailto_unload,
	ntfy_mailto_compile_check_uri,
	NULL,
	ntfy_mailto_compile_check_from,
	NULL,
	ntfy_mailto_runtime_check_uri,
	ntfy_mailto_runtime_get_notify_capability,
	ntfy_mailto_runtime_check_operands,
	NULL,
	ntfy_mailto_action_check_duplicates,
	ntfy_mailto_action_print,
	ntfy_mailto_action_execute,
};

/*
 * Reserved and unique headers
 */

static const char *_reserved_headers[] = {
	"auto-submitted",
	"received",
	"message-id",
	"data",
	"bcc",
	"in-reply-to",
	"references",
	"resent-date",
	"resent-from",
	"resent-sender",
	"resent-to",
	"resent-cc",
 	"resent-bcc",
	"resent-msg-id",
	"from",
	"sender",
	NULL
};

static const char *_unique_headers[] = {
	"reply-to",
	NULL
};

/*
 * Method context data
 */

struct ntfy_mailto_action_context {
	struct uri_mailto *uri;
	const struct smtp_address *from_address;
};

/*
 * Method registration
 */

struct ntfy_mailto_context {
	pool_t pool;
	struct sieve_address_source envelope_from;
};

static int
ntfy_mailto_load(const struct sieve_enotify_method *nmth, void **context_r)
{
	struct sieve_instance *svinst = nmth->svinst;
	struct ntfy_mailto_context *mtctx;
	pool_t pool;

	pool = pool_alloconly_create("ntfy_mailto_context", 256);
	mtctx = p_new(pool, struct ntfy_mailto_context, 1);
	mtctx->pool = pool;

	(void)sieve_address_source_parse_from_setting(
		svinst, mtctx->pool, "sieve_notify_mailto_envelope_from",
		&mtctx->envelope_from);

	*context_r = mtctx;
	return 0;
}

static void ntfy_mailto_unload(const struct sieve_enotify_method *nmth)
{
	struct ntfy_mailto_context *mtctx = nmth->context;

	pool_unref(&mtctx->pool);
}

/*
 * URI parsing
 */

struct ntfy_mailto_uri_env {
	const struct sieve_enotify_env *nenv;

	struct event *event;

	struct uri_mailto_log uri_log;
};

static void ATTR_FORMAT(5, 0)
ntfy_mailto_uri_logv(void *context, enum log_type log_type,
		     const char *csrc_filename, unsigned int csrc_linenum,
		     const char *fmt, va_list args)
{
	struct ntfy_mailto_uri_env *nmuenv = context;
	const struct sieve_enotify_env *nenv = nmuenv->nenv;

	sieve_event_logv(nenv->svinst, nenv->ehandler, nmuenv->event,
			 log_type, csrc_filename, csrc_linenum,
			 nenv->location, 0, fmt, args);
}

static void
ntfy_mailto_uri_env_init(struct ntfy_mailto_uri_env *nmuenv,
			 const struct sieve_enotify_env *nenv)
{
	i_zero(nmuenv);
	nmuenv->nenv = nenv;
	nmuenv->event = event_create(nenv->event);
	event_set_append_log_prefix(nmuenv->event, "mailto URI: ");

	nmuenv->uri_log.context = nmuenv;
	nmuenv->uri_log.logv = ntfy_mailto_uri_logv;
}

static void ntfy_mailto_uri_env_deinit(struct ntfy_mailto_uri_env *nmuenv)
{
	event_unref(&nmuenv->event);
}

/*
 * Validation
 */


static bool
ntfy_mailto_compile_check_uri(const struct sieve_enotify_env *nenv,
			      const char *uri ATTR_UNUSED, const char *uri_body)
{
	struct ntfy_mailto_uri_env nmuenv;
	bool result;

	ntfy_mailto_uri_env_init(&nmuenv, nenv);
	result = uri_mailto_validate(
		uri_body, _reserved_headers, _unique_headers,
		NTFY_MAILTO_MAX_RECIPIENTS, NTFY_MAILTO_MAX_HEADERS,
		&nmuenv.uri_log);
	ntfy_mailto_uri_env_deinit(&nmuenv);

	return result;
}

static bool
ntfy_mailto_compile_check_from(const struct sieve_enotify_env *nenv,
			       string_t *from)
{
	const char *error;
	bool result = FALSE;

	T_BEGIN {
		result = sieve_address_validate_str(from, &error);
		if (!result) {
			sieve_enotify_error(
				nenv,
				"specified :from address '%s' is invalid for "
				"the mailto method: %s",
				str_sanitize(str_c(from), 128), error);
		}
	} T_END;

	return result;
}

/*
 * Runtime
 */

struct ntfy_mailto_runtime_env {
	const struct sieve_enotify_env *nenv;

	struct event *event;
};

static const char *
ntfy_mailto_runtime_get_notify_capability(
	const struct sieve_enotify_env *nenv ATTR_UNUSED,
	const char *uri ATTR_UNUSED, const char *uri_body,
	const char *capability)
{
	if (!uri_mailto_validate(uri_body, _reserved_headers, _unique_headers,
				 NTFY_MAILTO_MAX_RECIPIENTS,
				 NTFY_MAILTO_MAX_HEADERS, NULL))
		return NULL;

	if (strcasecmp(capability, "online") == 0)
		return "maybe";

	return NULL;
}

static bool
ntfy_mailto_runtime_check_uri(const struct sieve_enotify_env *nenv ATTR_UNUSED,
			      const char *uri ATTR_UNUSED, const char *uri_body)
{
	return uri_mailto_validate(uri_body, _reserved_headers, _unique_headers,
				   NTFY_MAILTO_MAX_RECIPIENTS,
				   NTFY_MAILTO_MAX_HEADERS, NULL);
}

static bool
ntfy_mailto_runtime_check_operands(const struct sieve_enotify_env *nenv,
				   const char *uri ATTR_UNUSED,
				   const char *uri_body,
				   string_t *message ATTR_UNUSED,
				   string_t *from, pool_t context_pool,
				   void **method_context)
{
	struct ntfy_mailto_action_context *mtactx;
	struct uri_mailto *parsed_uri;
	const struct smtp_address *address;
	struct ntfy_mailto_uri_env nmuenv;
	const char *error;

	/* Need to create context before validation to have arrays present */
	mtactx = p_new(context_pool, struct ntfy_mailto_action_context, 1);

	/* Validate :from */
	if (from != NULL) {
		T_BEGIN {
			address = sieve_address_parse_str(from, &error);
			if (address == NULL) {
				sieve_enotify_error(
					nenv,
					"specified :from address '%s' is invalid for "
					"the mailto method: %s",
					str_sanitize(str_c(from), 128), error);
			} else {
				mtactx->from_address =
					smtp_address_clone(context_pool, address);
			}
		} T_END;

		if (address == NULL)
			return FALSE;
	}

	ntfy_mailto_uri_env_init(&nmuenv, nenv);
	parsed_uri = uri_mailto_parse(uri_body, context_pool,
				      _reserved_headers, _unique_headers,
				      NTFY_MAILTO_MAX_RECIPIENTS,
				      NTFY_MAILTO_MAX_HEADERS,
				      &nmuenv.uri_log);
	ntfy_mailto_uri_env_deinit(&nmuenv);

	if (parsed_uri == NULL)
		return FALSE;

	mtactx->uri = parsed_uri;
	*method_context = mtactx;
	return TRUE;
}

/*
 * Action duplicates
 */

static int
ntfy_mailto_action_check_duplicates(
	const struct sieve_enotify_env *nenv ATTR_UNUSED,
	const struct sieve_enotify_action *nact,
	const struct sieve_enotify_action *nact_other)
{
	struct ntfy_mailto_action_context *mtactx =
		nact->method_context;
	struct ntfy_mailto_action_context *mtactx_other =
		nact_other->method_context;
	const struct uri_mailto_recipient *new_rcpts, *old_rcpts;
	unsigned int new_count, old_count, i, j;
	unsigned int del_start = 0, del_len = 0;

	new_rcpts = array_get(&mtactx->uri->recipients, &new_count);
	old_rcpts = array_get(&mtactx_other->uri->recipients, &old_count);

	for (i = 0; i < new_count; i++) {
		for (j = 0; j < old_count; j++) {
			if (smtp_address_equals(new_rcpts[i].address,
						old_rcpts[j].address))
				break;
		}

		if (j == old_count) {
			/* Not duplicate */
			if (del_len > 0) {
				/* Perform pending deletion */
				array_delete(&mtactx->uri->recipients,
					     del_start, del_len);

				/* Make sure the loop integrity is maintained */
				i -= del_len;
				new_rcpts = array_get(&mtactx->uri->recipients,
						      &new_count);
			}
			del_len = 0;
		} else {
			/* Mark deletion */
			if (del_len == 0)
				del_start = i;
			del_len++;
		}
	}

	/* Perform pending deletion */
	if (del_len > 0)
		array_delete(&mtactx->uri->recipients, del_start, del_len);
	return (array_count(&mtactx->uri->recipients) > 0 ? 0 : 1);
}

/*
 * Action printing
 */

static void
ntfy_mailto_action_print(const struct sieve_enotify_print_env *penv,
			 const struct sieve_enotify_action *nact)
{
	unsigned int count, i;
	const struct uri_mailto_recipient *recipients;
	const struct uri_mailto_header_field *headers;
	struct ntfy_mailto_action_context *mtactx = nact->method_context;

	/* Print main method parameters */

	sieve_enotify_method_printf(penv, "    => importance   : %llu\n",
				    (unsigned long long)nact->importance);

	if (nact->message != NULL) {
		sieve_enotify_method_printf(
			penv, "    => subject      : %s\n",
			nact->message);
	} else if (mtactx->uri->subject != NULL) {
		sieve_enotify_method_printf(
			penv, "    => subject      : %s\n",
			mtactx->uri->subject);
	}

	if (nact->from != NULL) {
		sieve_enotify_method_printf(
			penv, "    => from         : %s\n", nact->from);
	}

	/* Print mailto: recipients */

	sieve_enotify_method_printf(penv,   "    => recipients   :\n");

	recipients = array_get(&mtactx->uri->recipients, &count);
	if (count == 0) {
		sieve_enotify_method_printf(
			penv,   "       NONE, action has no effect\n");
	} else {
		for (i = 0; i < count; i++) {
			if (recipients[i].carbon_copy) {
				sieve_enotify_method_printf(
					penv, "       + Cc: %s\n",
					recipients[i].full);
			} else {
				sieve_enotify_method_printf(
					penv,   "       + To: %s\n",
					recipients[i].full);
			}
		}
	}

	/* Print accepted headers for notification message */

	headers = array_get(&mtactx->uri->headers, &count);
	if (count > 0) {
		sieve_enotify_method_printf(penv,   "    => headers      :\n");
		for (i = 0; i < count; i++) {
			sieve_enotify_method_printf(
				penv, "       + %s: %s\n",
				headers[i].name, headers[i].body);
		}
	}

	/* Print body for notification message */

	if (mtactx->uri->body != NULL) {
		sieve_enotify_method_printf(
			penv, "    => body         : \n--\n%s\n--\n",
			mtactx->uri->body);
	}

	/* Finish output with an empty line */

	sieve_enotify_method_printf(penv,   "\n");
}

/*
 * Action execution
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

static int
ntfy_mailto_send(const struct sieve_enotify_exec_env *nenv,
		 const struct sieve_enotify_action *nact,
		 const struct smtp_address *owner_email)
{
	struct sieve_instance *svinst = nenv->svinst;
	const struct sieve_message_data *msgdata = nenv->msgdata;
	const struct sieve_script_env *senv = nenv->scriptenv;
	struct ntfy_mailto_action_context *mtactx = nact->method_context;
	struct ntfy_mailto_context *mtctx = nenv->method->context;
	struct sieve_address_source env_from = mtctx->envelope_from;
	const char *from = NULL;
	const struct smtp_address *from_smtp = NULL;
	const char *subject = mtactx->uri->subject;
	const char *body = mtactx->uri->body;
	string_t *to, *cc, *all;
	const struct uri_mailto_recipient *recipients;
	const struct uri_mailto_header_field *headers;
	struct sieve_smtp_context *sctx;
	struct ostream *output;
	string_t *msg;
	unsigned int count, i, hcount, h;
	const char *outmsgid, *error;
	int ret;

	/* Get recipients */
	recipients = array_get(&mtactx->uri->recipients, &count);
	if (count == 0) {
		sieve_enotify_warning(
			nenv, "notify mailto uri specifies no recipients; "
			"action has no effect");
		return 0;
	}

	/* Just to be sure */
	if (!sieve_smtp_available(senv)) {
		sieve_enotify_global_warning(
			nenv,
			"notify mailto method has no means to send mail");
		return 0;
	}

	/* Determine which sender to use

	   From RFC 5436, Section 2.3:

	   The ":from" tag overrides the default sender of the notification
	   message.  "Sender", here, refers to the value used in the [RFC5322]
	   "From" header.  Implementations MAY also use this value in the
	   [RFC5321] "MAIL FROM" command (the "envelope sender"), or they may
	   prefer to establish a mailbox that receives bounces from notification
	   messages.
	 */
	if ((nenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0) {
		from_smtp = sieve_message_get_sender(nenv->msgctx);
		if (from_smtp == NULL) {
			/* "<>" */
			i_zero(&env_from);
			env_from.type = SIEVE_ADDRESS_SOURCE_EXPLICIT;
		}
	}
	from = nact->from;
	ret = sieve_address_source_get_address(
		&env_from, svinst, senv, nenv->msgctx, nenv->flags, &from_smtp);
	if (ret < 0) {
		from_smtp = NULL;
	} else if (ret == 0) {
		if (mtactx->from_address != NULL)
			from_smtp = mtactx->from_address;
		else if (svinst->user_email != NULL)
			from_smtp = svinst->user_email;
		else {
			from_smtp = sieve_get_postmaster_smtp(senv);
			if (from == NULL)
				from = sieve_get_postmaster_address(senv);
		}
	}

	/* Determine message from address */
	if (from == NULL) {
		if (from_smtp == NULL)
			from = sieve_get_postmaster_address(senv);
		else {
			from = t_strdup_printf("<%s>",
				smtp_address_encode(from_smtp));
		}
	}

	/* Determine subject */
	if (nact->message != NULL) {
		subject = str_sanitize_utf8(
			nact->message, SIEVE_MAX_SUBJECT_HEADER_CODEPOINTS);
	} else if (subject == NULL) {
		const char *const *hsubject;

		/* Fetch subject from original message */
		if (mail_get_headers_utf8(msgdata->mail, "subject",
					  &hsubject) > 0) {
			subject = str_sanitize_utf8(
				t_strdup_printf("Notification: %s", hsubject[0]),
				SIEVE_MAX_SUBJECT_HEADER_CODEPOINTS);
		} else {
			subject = "Notification: (no subject)";
		}
	}

	/* Compose To and Cc headers */
	to = NULL;
	cc = NULL;
	all = t_str_new(256);
	for (i = 0; i < count; i++) {
		if (recipients[i].carbon_copy) {
			if (cc == NULL) {
				cc = t_str_new(256);
				str_append(cc, recipients[i].full);
			} else {
				str_append(cc, ", ");
				str_append(cc, recipients[i].full);
			}
		} else {
			if (to == NULL) {
				to = t_str_new(256);
				str_append(to, recipients[i].full);
			} else {
				str_append(to, ", ");
				str_append(to, recipients[i].full);
			}
		}
		if (i < 3) {
			if (i > 0)
				str_append(all, ", ");
			str_append(
				all, smtp_address_encode_path(
					recipients[i].address));
		} else if (i == 3) {
			str_printfa(all, ", ... (%u total)", count);
		}
	}

	msg = t_str_new(512);
	outmsgid = sieve_message_get_new_id(svinst);

	rfc2822_header_write(msg, "X-Sieve", SIEVE_IMPLEMENTATION);
	rfc2822_header_write(msg, "Message-ID", outmsgid);
	rfc2822_header_write(msg, "Date", message_date_create(ioloop_time));
	rfc2822_header_utf8_printf(msg, "Subject", "%s", subject);

	rfc2822_header_write_address(msg, "From", from);

	if (to != NULL)
		rfc2822_header_write_address(msg, "To", str_c(to));
	if (cc != NULL)
		rfc2822_header_write_address(msg, "Cc", str_c(cc));

	rfc2822_header_printf(msg, "Auto-Submitted",
			      "auto-notified; owner-email=\"%s\"",
			      smtp_address_encode(owner_email));
	rfc2822_header_write(msg, "Precedence", "bulk");

	/* Set importance */
	switch (nact->importance) {
	case 1:
		rfc2822_header_write(msg, "X-Priority", "1 (Highest)");
		rfc2822_header_write(msg, "Importance", "High");
		break;
	case 3:
		rfc2822_header_write(msg, "X-Priority", "5 (Lowest)");
		rfc2822_header_write(msg, "Importance", "Low");
		break;
	case 2:
	default:
		rfc2822_header_write(msg, "X-Priority", "3 (Normal)");
		rfc2822_header_write(msg, "Importance", "Normal");
		break;
	}

	/* Add custom headers */

	headers = array_get(&mtactx->uri->headers, &hcount);
	for (h = 0; h < hcount; h++) {
		const char *name =
			rfc2822_header_field_name_sanitize(headers[h].name);

		rfc2822_header_write(msg, name, headers[h].body);
	}

	/* Generate message body */

	rfc2822_header_write(msg, "MIME-Version", "1.0");
	if (body != NULL) {
		if (_contains_8bit(body)) {
			rfc2822_header_write(msg, "Content-Type",
					     "text/plain; charset=utf-8");
			rfc2822_header_write(msg, "Content-Transfer-Encoding",
					     "8bit");
		} else {
			rfc2822_header_write(msg, "Content-Type",
					     "text/plain; charset=us-ascii");
			rfc2822_header_write(msg, "Content-Transfer-Encoding",
					     "7bit");
		}
		str_printfa(msg, "\r\n%s\r\n", body);

	} else {
		rfc2822_header_write(msg, "Content-Type",
				     "text/plain; charset=US-ASCII");
		rfc2822_header_write(msg, "Content-Transfer-Encoding",
				     "7bit");

		str_append(msg, "\r\nNotification of new message.\r\n");
	}

	sctx = sieve_smtp_start(senv, from_smtp);

	/* Send message to all recipients */
	for (i = 0; i < count; i++)
		sieve_smtp_add_rcpt(sctx, recipients[i].address);

	output = sieve_smtp_send(sctx);
	o_stream_nsend(output, str_data(msg), str_len(msg));

	if ((ret = sieve_smtp_finish(sctx, &error)) <= 0) {
		if (ret < 0) {
			sieve_enotify_global_error(
				nenv, "failed to send mail notification to %s: "
				"%s (temporary failure)",
				str_c(all), str_sanitize(error, 512));
		} else {
			sieve_enotify_global_log_error(
				nenv, "failed to send mail notification to %s: "
				"%s (permanent failure)",
				str_c(all), str_sanitize(error, 512));
		}
	} else {
		struct event_passthrough *e =
			sieve_enotify_create_finish_event(nenv)->
			add_str("notify_target", str_c(all));

		sieve_enotify_event_log(nenv, e->event(),
					"sent mail notification to %s",
					str_c(all));
	}

	return 0;
}

static int
ntfy_mailto_action_execute(const struct sieve_enotify_exec_env *nenv,
			   const struct sieve_enotify_action *nact)
{
	struct sieve_instance *svinst = nenv->svinst;
	const struct sieve_script_env *senv = nenv->scriptenv;
	struct mail *mail = nenv->msgdata->mail;
	const struct smtp_address *owner_email;
	const char *const *hdsp;
	int ret;

	owner_email = svinst->user_email;
	if (owner_email == NULL &&
	    (nenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0)
		owner_email = sieve_message_get_final_recipient(nenv->msgctx);
	if (owner_email == NULL)
		owner_email = sieve_get_postmaster_smtp(senv);
	i_assert(owner_email != NULL);

	/* Is the message an automatic reply ? */
	ret = mail_get_headers(mail, "auto-submitted", &hdsp);
	if (ret < 0) {
		sieve_enotify_critical(
			nenv, "mailto notification: "
			"failed to read 'auto-submitted' header field",
			"mailto notification: "
			"failed to read 'auto-submitted' header field: %s",
			mailbox_get_last_internal_error(mail->box, NULL));
		return -1;
	}

	/* Theoretically multiple headers could exist, so lets make sure */
	if (ret > 0) {
		while (*hdsp != NULL) {
			if (strcasecmp(*hdsp, "no") != 0) {
				const struct smtp_address *sender = NULL;
				const char *from;

				if ((nenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0)
					sender = sieve_message_get_sender(nenv->msgctx);
				from = (sender == NULL ? "" :
					t_strdup_printf(" from <%s>",
							smtp_address_encode(sender)));

				sieve_enotify_global_info(
					nenv, "not sending notification "
					"for auto-submitted message%s", from);
				return 0;
			}
			hdsp++;
		}
	}

	T_BEGIN {
		ret = ntfy_mailto_send(nenv, nact, owner_email);
	} T_END;

	return ret;
}
