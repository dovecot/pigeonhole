/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */
 
#include "lib.h"
#include "array.h"
#include "str.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "message-date.h"

#include "rfc2822.h"

#include "sieve-common.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-interpreter.h"
#include "sieve-actions.h"
#include "sieve-result.h"

#include "sieve-ext-enotify.h"

/*
 * Configuration
 */
 
#define NTFY_MAILTO_MAX_RECIPIENTS 4
#define NTFY_MAILTO_MAX_HEADERS 16

/* 
 * Types 
 */

struct ntfy_mailto_header_field {
	const char *name;
	const char *body;
};

ARRAY_DEFINE_TYPE(recipients, const char *);
ARRAY_DEFINE_TYPE(headers, struct ntfy_mailto_header_field);

/* 
 * Mailto notification method
 */
 
static bool ntfy_mailto_validate_uri
	(const struct sieve_enotify_log_context *nlctx, const char *uri, 
		const char *uri_body);
static bool ntfy_mailto_runtime_check_operands
	(const struct sieve_enotify_log_context *nlctx, const char *uri,
		const char *uri_body, const char *message, const char *from, 
		pool_t context_pool, void **context);
static void ntfy_mailto_action_print
	(const struct sieve_result_print_env *rpenv, 
		const struct sieve_enotify_action *act);	
static bool ntfy_mailto_action_execute
	(const struct sieve_enotify_exec_env *nenv, 
		const struct sieve_enotify_action *act);

const struct sieve_enotify_method mailto_notify = {
	"mailto",
	ntfy_mailto_validate_uri,
	ntfy_mailto_runtime_check_operands,
	ntfy_mailto_action_print,
	ntfy_mailto_action_execute
};

/*
 * Method context data
 */
 
struct ntfy_mailto_context {
	ARRAY_TYPE(recipients) recipients;
	ARRAY_TYPE(headers) headers;
	const char *subject;
	const char *body;
};

/*
 * Reserved headers
 */
 
static const char *_reserved_headers[] = {
	"auto-submitted",
	"received",
	"message-id",
	"data",
	NULL
};

static inline bool _ntfy_mailto_header_allowed(const char *field_name)
{
	const char **rhdr = _reserved_headers;

	/* Check whether it is reserved */
	while ( *rhdr != NULL ) {
		if ( strcasecmp(field_name, *rhdr) == 0 )
			return FALSE;
		rhdr++;
	}

	return TRUE;
}

/*
 * Mailto URI parsing
 */
 
/* FIXME: much of this implementation will be common to other URI schemes. This
 *        should be merged into a common implementation.
 */

static const char _qchar_lookup[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 00
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 10
	0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0,  // 20
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,  // 30
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 40
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,  // 50
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 60
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,  // 70

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 80
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 90
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A0
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B0
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C0
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D0
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E0
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // F0
};

static inline bool _is_qchar(unsigned char c)
{
	return _qchar_lookup[c];
}
  
static inline int _decode_hex_digit(char digit)
{
	switch ( digit ) {
	case '0': case '1': case '2': case '3': case '4': 
	case '5': case '6': case '7': case '8': case '9': 
		return (int) digit - '0';

	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
		return (int) digit - 'a' + 0x0a;
		
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
		return (int) digit - 'A' + 0x0A;
	}
	
	return -1;
}

static bool _parse_hex_value(const char **in, char *out)
{
	char value;
		
	if ( **in == '\0' || (value=_decode_hex_digit(**in)) < 0 )
		return FALSE;
	
	*out = value << 4;
	(*in)++;
	
	if ( **in == '\0' || (value=_decode_hex_digit(**in)) < 0 )
		return FALSE;	

	*out |= value;
	(*in)++;
	return (*out != '\0');	
}

static inline pool_t array_get_pool_i(struct array *array)
{
	return buffer_get_pool(array->buffer);
}
#define array_get_pool(array) \
	array_get_pool_i(&(array)->arr)

static bool _uri_parse_recipients
(const char **uri_p, ARRAY_TYPE(recipients) *recipients_r, const char **error_r)
{
	string_t *to = t_str_new(128);
	const char *recipient;
	const char *p = *uri_p;
	pool_t pool = NULL;
	
	if ( recipients_r != NULL )
		pool = array_get_pool(recipients_r);
		
	while ( *p != '\0' && *p != '?' ) {
		if ( *p == '%' ) {
			/* % encoded character */
			char ch;
			
			p++;
			
			/* Parse 2-digit hex value */
			if ( !_parse_hex_value(&p, &ch) ) {
				*error_r = "invalid % encoding";
				return FALSE;
			}

			/* Check for delimiter */
			if ( ch == ',' ) {
				recipient = str_c(to);
				
				/* Verify recipient */
			
				// FIXME ....
				
				/* Add recipient to the list */
				if ( recipients_r != NULL ) {
					recipient = p_strdup(pool, recipient);
					array_append(recipients_r, &recipient, 1);
				}
			
				/* Reset for next recipient */
				str_truncate(to, 0);
			}	else {
				/* Content character */
				str_append_c(to, ch);
			}
		} else {
			/* Content character */
			str_append_c(to, *p);
			p++;
		}
	}	
	
	/* Skip '?' */
	if ( *p != '\0' ) p++;
	
	recipient = str_c(to);
	
	/* Verify recipient */

	// FIXME ....
		
	if ( recipients_r != NULL ) {
		/* Add recipient to the list */
		recipient = p_strdup(pool, recipient);
		array_append(recipients_r, &recipient, 1);
	}
	
	*uri_p = p;
	return TRUE;
}

static bool _uri_parse_headers
(const char **uri_p, ARRAY_TYPE(headers) *headers_r, const char **body, 
	const char **subject, const char **error_r)
{
	string_t *field = t_str_new(128);
	const char *p = *uri_p;
	pool_t pool = NULL;
	
	if ( headers_r != NULL )
		pool = array_get_pool(headers_r);
		
	while ( *p != '\0' ) {
		enum { _HNAME_GENERIC, _HNAME_SUBJECT, _HNAME_BODY } hname_type = 
			_HNAME_GENERIC;
		struct ntfy_mailto_header_field *hdrf = NULL;
		const char *field_name;
		
		/* Parse field name */
		while ( *p != '\0' && *p != '=' ) {
			char ch = *p;
			p++;
			
			if ( ch == '%' ) {
				/* Encoded, parse 2-digit hex value */
				if ( !_parse_hex_value(&p, &ch) ) {
					printf("F: %s\n", p);
					*error_r = "invalid % encoding";
					return FALSE;
				}
			} else if ( ch != '=' && !_is_qchar(ch) ) {
				*error_r = t_strdup_printf
					("invalid character '%c' in header field name part", *p);
				return FALSE;
			}

			str_append_c(field, ch);
		}
		if ( *p != '\0' ) p++;

		/* Verify field name */
		if ( !rfc2822_header_field_name_verify(str_c(field), str_len(field)) ) {
			*error_r = "invalid header field name";
			return FALSE;
		}

		/* Add new header field to array and assign its name */
		field_name = str_c(field);
		if ( strcasecmp(field_name, "subject") == 0 )
			hname_type = _HNAME_SUBJECT;
		else if ( strcasecmp(field_name, "body") == 0 )
			hname_type = _HNAME_BODY;
		else if ( _ntfy_mailto_header_allowed(field_name) ) {
			if ( headers_r != NULL ) {
				hdrf = array_append_space(headers_r);
				hdrf->name = p_strdup(pool, field_name);
			}
		} else {
			hdrf = NULL;
		}
		
		/* Reset for field body */
		str_truncate(field, 0);
		
		/* Parse field body */		
		while ( *p != '\0' && *p != '&' ) {
			char ch = *p;
			p++;
			
			if ( ch == '%' ) {
				/* Encoded, parse 2-digit hex value */
				if ( !_parse_hex_value(&p, &ch) ) {
					printf("F: %s\n", p);
					*error_r = "invalid % encoding";
					return FALSE;
				}
			} else if ( ch != '=' && !_is_qchar(ch) ) {
				*error_r = t_strdup_printf
					("invalid character '%c' in header field value part", *p);
				return FALSE;
			}
			str_append_c(field, ch);
		}
		if ( *p != '\0' ) p++;
		
		/* Verify field body */
		if ( hname_type == _HNAME_BODY ) {
			// FIXME: verify body ... 
		} else {
			if ( !rfc2822_header_field_body_verify(str_c(field), str_len(field)) ) {
				*error_r = "invalid header field body";
				return FALSE;
			}
		}
		
		/* Assign field body */

		if ( headers_r != NULL ) {
			switch ( hname_type ) {
			case _HNAME_SUBJECT:
				if ( subject != NULL )
					*subject = p_strdup(pool, str_c(field));
				break;
			case _HNAME_BODY:
				if ( subject != NULL )
					*body = p_strdup(pool, str_c(field));
				break;
			case _HNAME_GENERIC:
				if ( hdrf != NULL ) 
					hdrf->body = p_strdup(pool, str_c(field));
				break;
			}
		}
			
		/* Reset for next name */
		str_truncate(field, 0);
	}	
	
	/* Skip '&' */
	if ( *p != '\0' ) p++;

	*uri_p = p;
	return TRUE;
}

static bool ntfy_mailto_parse_uri
(const char *uri_body, ARRAY_TYPE(recipients) *recipients_r, 
	ARRAY_TYPE(headers) *headers_r, const char **body, const char **subject,
	const char **error_r)
{
	const char *p = uri_body;
	
	/* 
   * mailtoURI   = "mailto:" [ to ] [ hfields ]
   * to          = [ addr-spec *("%2C" addr-spec ) ]
   * hfields     = "?" hfield *( "&" hfield )
   * hfield      = hfname "=" hfvalue
   * hfname      = *qchar
   * hfvalue     = *qchar
   * addr-spec   = local-part "@" domain
   * local-part  = dot-atom / quoted-string
   * qchar       = unreserved / pct-encoded / some-delims
   * some-delims = "!" / "$" / "'" / "(" / ")" / "*"
   *               / "+" / "," / ";" / ":" / "@"
   *
	 * Scheme 'mailto:' already parsed, starting parse after colon
	 */

	/* First extract to-part by searching for '?' and decoding % items
	 */

	if ( !_uri_parse_recipients(&p, recipients_r, error_r) )
		return FALSE;	

	/* Extract hfield items */	
	
	while ( *p != '\0' ) {		
		/* Extract hfield item by searching for '&' and decoding '%' items */
		if ( !_uri_parse_headers(&p, headers_r, body, subject, error_r) )
			return FALSE;		
	}
	
	return TRUE;
}

/*
 * Validation
 */

static bool ntfy_mailto_validate_uri
(const struct sieve_enotify_log_context *nlctx, const char *uri,
	const char *uri_body)
{	
	const char *error;
	
	if ( !ntfy_mailto_parse_uri(uri_body, NULL, NULL, NULL, NULL, &error) ) {
		sieve_enotify_error
			(nlctx,  "invalid mailto URI '%s': %s", str_sanitize(uri, 80), error);
		return FALSE;
	}
	
	return TRUE;
}

/*
 * Runtime
 */
 
static bool ntfy_mailto_runtime_check_operands
(const struct sieve_enotify_log_context *nlctx, const char *uri, 
	const char *uri_body, const char *message ATTR_UNUSED, 
	const char *from ATTR_UNUSED, pool_t context_pool, void **context)
{
	struct ntfy_mailto_context *mtctx;
	const char *error;
	
	/* Need to create context before validation to have arrays present */
	mtctx = p_new(context_pool, struct ntfy_mailto_context, 1);
	p_array_init(&mtctx->recipients, context_pool, NTFY_MAILTO_MAX_RECIPIENTS);
	p_array_init(&mtctx->headers, context_pool, NTFY_MAILTO_MAX_HEADERS);

	if ( !ntfy_mailto_parse_uri
		(uri_body, &mtctx->recipients, &mtctx->headers, &mtctx->body, 
			&mtctx->subject, &error) ) {
		sieve_enotify_error
			(nlctx, "invalid mailto URI '%s': %s", str_sanitize(uri, 80), error);
		return FALSE;
	}
	
	*context = (void *) mtctx;

	return TRUE;	
}

/*
 * Action printing
 */
 
static void ntfy_mailto_action_print
(const struct sieve_result_print_env *rpenv, 
	const struct sieve_enotify_action *act)
{
	unsigned int count, i;
	const char *const *recipients;
	const struct ntfy_mailto_header_field *headers;
	struct ntfy_mailto_context *mtctx = 
		(struct ntfy_mailto_context *) act->method_context;
	
	sieve_result_printf(rpenv,   "    => importance   : %d\n", act->importance);
	if ( act->message != NULL )
		sieve_result_printf(rpenv, "    => subject      : %s\n", act->message);
	else if ( mtctx->subject != NULL )
		sieve_result_printf(rpenv, "    => subject      : %s\n", mtctx->subject);
	if ( act->from != NULL )
		sieve_result_printf(rpenv, "    => from         : %s\n", act->from);

	sieve_result_printf(rpenv,   "    => recipients   :\n" );
	recipients = array_get(&mtctx->recipients, &count);
	for ( i = 0; i < count; i++ ) {
		sieve_result_printf(rpenv,   "       + %s\n", recipients[i]);
	}
	
	sieve_result_printf(rpenv,   "    => headers      :\n" );
	headers = array_get(&mtctx->headers, &count);
	for ( i = 0; i < count; i++ ) {
		sieve_result_printf(rpenv,   "       + %s: %s\n", 
			headers[i].name, headers[i].body);
	}
	
	if ( mtctx->body != NULL )
		sieve_result_printf(rpenv, "    => body         : \n--\n%s\n--\n\n", 
			mtctx->body);
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

static bool ntfy_mailto_send
(const struct sieve_enotify_exec_env *nenv, 
	const struct sieve_enotify_action *act)
{ 
	const struct sieve_enotify_log_context *nlctx = nenv->logctx;
	const struct sieve_message_data *msgdata = nenv->msgdata;
	const struct sieve_script_env *senv = nenv->scriptenv;
	struct ntfy_mailto_context *mtctx = 
		(struct ntfy_mailto_context *) act->method_context;	
	const char *from = NULL; 
	const char *subject = mtctx->subject;
	const char *body = mtctx->body;
	const char *const *recipients;
	void *smtp_handle;
	unsigned int count, i;
	FILE *f;
	const char *outmsgid;

	/* Just to be sure */
	if ( senv->smtp_open == NULL || senv->smtp_close == NULL ) {
		sieve_enotify_warning(nlctx, 
			"notify mailto method has no means to send mail.");
		return TRUE;
	}
	
	/* Determine from address */
	if ( msgdata->return_path != NULL ) {
		if ( act->from == NULL )
			from = senv->postmaster_address;
		else
			/* FIXME: validate */
			from = act->from;
	}
	
	/* Determine subject */
	if ( act->message != NULL ) {
		/* FIXME: handle UTF-8 */
		subject = str_sanitize(act->message, 256);
	} else if ( subject == NULL ) {
		const char *const *hsubject;
		
		/* Fetch subject from original message */
		if ( mail_get_headers_utf8
			(msgdata->mail, "subject", &hsubject) >= 0 )
			subject = t_strdup_printf("Notification: %s", hsubject[0]);
		else
			subject = "Notification: (no subject)";
	}

	/* Send message to all recipients */
	recipients = array_get(&mtctx->recipients, &count);
	for ( i = 0; i < count; i++ ) {
		const struct ntfy_mailto_header_field *headers;
		unsigned int h, hcount;

		smtp_handle = senv->smtp_open(recipients[i], from, &f);
		outmsgid = sieve_get_new_message_id(senv);
	
		rfc2822_header_field_write(f, "X-Sieve", SIEVE_IMPLEMENTATION);
		rfc2822_header_field_write(f, "Message-ID", outmsgid);
		rfc2822_header_field_write(f, "Date", message_date_create(ioloop_time));
		rfc2822_header_field_printf(f, "From", "<%s>", from);
		rfc2822_header_field_printf(f, "To", "<%s>", recipients[i]);
		rfc2822_header_field_write(f, "Subject", subject);
			
		rfc2822_header_field_printf(f, "Auto-Submitted", 
			"auto-notified; owner-email=\"%s\"", msgdata->to_address);
		rfc2822_header_field_write(f, "Precedence", "bulk");

		/* Set importance */
		switch ( act->importance ) {
		case 1:
			rfc2822_header_field_write(f, "X-Priority", "1 (Highest)");
			rfc2822_header_field_write(f, "Importance", "High");
			break;
		case 3:
		  rfc2822_header_field_write(f, "X-Priority", "5 (Lowest)");
		  rfc2822_header_field_write(f, "Importance", "Low");
		  break;
		case 2:
		default:
			rfc2822_header_field_write(f, "X-Priority", "3 (Normal)");
			rfc2822_header_field_write(f, "Importance", "Normal");
			break;
		}
		
		/* Add custom headers */
		
		headers = array_get(&mtctx->headers, &hcount);
		for ( h = 0; h < hcount; h++ ) {
			const char *name = rfc2822_header_field_name_sanitize(headers[h].name);
		
			rfc2822_header_field_write(f, name, headers[h].body);
		}
			
		/* Generate message body */
		if ( body != NULL ) {
			if (_contains_8bit(body)) {
				rfc2822_header_field_write(f, "MIME-Version", "1.0");
				rfc2822_header_field_write
					(f, "Content-Type", "text/plain; charset=UTF-8");
				rfc2822_header_field_write(f, "Content-Transfer-Encoding", "8bit");
			}
			
			fprintf(f, "\r\n");
			fprintf(f, "%s\r\n", body);
			
		} else {
			fprintf(f, "\r\n");
			fprintf(f, "Notification of new message.\r\n");
		}
	
		if ( senv->smtp_close(smtp_handle) ) {
			sieve_enotify_log(nlctx, 
				"sent mail notification to <%s>", str_sanitize(recipients[i], 80));
		} else {
			sieve_enotify_error(nlctx,
				"failed to send mail notification to <%s> "
				"(refer to system log for more information)", 
				str_sanitize(recipients[i], 80));
		}
	}

	return TRUE;
}

static bool ntfy_mailto_action_execute
(const struct sieve_enotify_exec_env *nenv, 
	const struct sieve_enotify_action *act)
{
	const struct sieve_message_data *msgdata = nenv->msgdata;
	const char *const *headers;

	/* Is the message an automatic reply ? */
	if ( mail_get_headers
		(msgdata->mail, "auto-submitted", &headers) >= 0 ) {
		const char *const *hdsp = headers;

		/* Theoretically multiple headers could exist, so lets make sure */
		while ( *hdsp != NULL ) {
			if ( strcasecmp(*hdsp, "no") != 0 ) {
				sieve_enotify_log(nenv->logctx, 
					"not sending notification for auto-submitted message from <%s>", 
					str_sanitize(msgdata->return_path, 128));	
					return TRUE;				 
			}
			hdsp++;
		}
	}

	return ntfy_mailto_send(nenv, act);
}



