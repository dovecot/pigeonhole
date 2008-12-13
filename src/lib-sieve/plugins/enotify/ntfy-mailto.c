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
	(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
		const char *uri_body);
static bool ntfy_mailto_runtime_check_operands
	(const struct sieve_runtime_env *renv, unsigned int source_line, 
		const char *uri, const char *uri_body, const char *message, 
		const char *from, void **context);
static void ntfy_mailto_action_print
	(const struct sieve_result_print_env *rpenv, 
		const struct sieve_enotify_context *nctx);	
static bool ntfy_mailto_action_execute
	(const struct sieve_action_exec_env *aenv, 
		const struct sieve_enotify_context *nctx);

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
};

/*
 * Mailto URI parsing
 */
 
/* FIXME: much of this implementation will be common to other URI schemes. This
 *        should be merged into a common implementation.
 */
  
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
(const char **uri_p, ARRAY_TYPE(headers) *headers_r, const char **error_r)
{
	string_t *field = t_str_new(128);
	const char *p = *uri_p;
	pool_t pool = NULL;
	
	if ( headers_r != NULL )
		pool = array_get_pool(headers_r);
		
	while ( *p != '\0' ) {
		struct ntfy_mailto_header_field *hdrf = NULL;
		
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
		if ( headers_r != NULL ) {
			hdrf = array_append_space(headers_r);
			hdrf->name = p_strdup(pool, str_c(field));
		}
		
		/* Reset for body */
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
			}
			str_append_c(field, ch);
		}
		if ( *p != '\0' ) p++;
		
		/* Verify field body */
		
		// FIXME ....
		
		/* Assign field body */
		
		if ( headers_r != NULL ) {
			hdrf->body = p_strdup(pool, str_c(field));
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
	ARRAY_TYPE(headers) *headers_r, const char **error_r)
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
		if ( !_uri_parse_headers(&p, headers_r, error_r) )
			return FALSE;		
	}
	
	return TRUE;
}

/*
 * Validation
 */

static bool ntfy_mailto_validate_uri
(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
	const char *uri_body)
{	
	const char *error;
	
	if ( !ntfy_mailto_parse_uri(uri_body, NULL, NULL, &error) ) {
		sieve_argument_validate_error(valdtr, arg, 
			"invalid mailto URI '%s': %s", 
			str_sanitize(sieve_ast_argument_strc(arg), 80), error);
		return FALSE;
	}
	
	return TRUE;
}

/*
 * Runtime
 */
 
static bool ntfy_mailto_runtime_check_operands
(const struct sieve_runtime_env *renv, unsigned int source_line, 
  const char *mailto_uri, const char *uri_body, 
  const char *message ATTR_UNUSED, const char *from ATTR_UNUSED, 
  void **context)
{
	pool_t pool;
	struct ntfy_mailto_context *nctx;
	const char *error;
	
	/* Need to create context before validation to have arrays present */
	pool = sieve_result_pool(renv->result);
	nctx = p_new(pool, struct ntfy_mailto_context, 1);
	p_array_init(&nctx->recipients, pool, NTFY_MAILTO_MAX_RECIPIENTS);
	p_array_init(&nctx->headers, pool, NTFY_MAILTO_MAX_HEADERS);

	if ( !ntfy_mailto_parse_uri
		(uri_body, &nctx->recipients, &nctx->headers, &error) ) {
		sieve_runtime_error
			(renv, sieve_error_script_location(renv->script, source_line),
				"invalid mailto URI '%s': %s", str_sanitize(mailto_uri, 80), error);
		return FALSE;
	}
	
	*context = (void *) nctx;

	return TRUE;	
}

/*
 * Action printing
 */
 
static void ntfy_mailto_action_print
(const struct sieve_result_print_env *rpenv, 
	const struct sieve_enotify_context *nctx)
{
	unsigned int count, i;
	const char *const *recipients;
	const struct ntfy_mailto_header_field *headers;
	struct ntfy_mailto_context *mtctx = 
		(struct ntfy_mailto_context *) nctx->method_context;
	
	sieve_result_printf(rpenv,   "    => importance   : %d\n", nctx->importance);
	if ( nctx->message != NULL )
		sieve_result_printf(rpenv, "    => message      : \n%s\n", nctx->message);
	if ( nctx->from != NULL )
		sieve_result_printf(rpenv, "    => from         : %s\n", nctx->from);

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
 
static bool ntfy_mailto_action_execute
(const struct sieve_action_exec_env *aenv, 
	const struct sieve_enotify_context *nctx)
{ 
	const struct sieve_message_data *msgdata = aenv->msgdata;
	const struct sieve_script_env *senv = aenv->scriptenv;
	struct ntfy_mailto_context *mtctx = 
		(struct ntfy_mailto_context *) nctx->method_context;	
	const char *const *recipients;
	void *smtp_handle;
	unsigned int count, i;
	FILE *f;
	const char *outmsgid;

	/* Just to be sure */
	if ( senv->smtp_open == NULL || senv->smtp_close == NULL ) {
		sieve_result_warning(aenv, 
			"notify mailto method has no means to send mail.");
		return FALSE;
	}

	recipients = array_get(&mtctx->recipients, &count);
	for ( i = 0; i < count; i++ ) {
		smtp_handle = senv->smtp_open(recipients[i], NULL, &f);
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
		fprintf(f, "To: <%s>\r\n", recipients[i]);
		fprintf(f, "Subject: [SIEVE] New mail notification\r\n");
		fprintf(f, "Auto-Submitted: auto-generated (notify)\r\n");
		fprintf(f, "Precedence: bulk\r\n");
	
		if ( nctx->message != NULL ) {
			if (_contains_8bit(nctx->message)) {
					fprintf(f, "MIME-Version: 1.0\r\n");
					fprintf(f, "Content-Type: text/plain; charset=UTF-8\r\n");
					fprintf(f, "Content-Transfer-Encoding: 8bit\r\n");
			}
			fprintf(f, "\r\n");
			fprintf(f, "%s\r\n", nctx->message);
		} else {
			fprintf(f, "\r\n");
			fprintf(f, "Notification of new message.\r\n");
		}
	
		if ( senv->smtp_close(smtp_handle) ) {
			sieve_result_log(aenv, 
				"sent mail notification to <%s>", str_sanitize(recipients[i], 80));
		} else {
			sieve_result_error(aenv,
				"failed to send mail notification to <%s> "
				"(refer to system log for more information)", 
				str_sanitize(recipients[i], 80));
		}
	}

	return TRUE;
}

