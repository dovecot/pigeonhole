/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */
 
/* Notify method mailto
 * --------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-notify-mailto-10.txt
 * Implementation: almost full
 * Status: under development
 * 
 */
 
/* FIXME: URI syntax conforms to something somewhere in between RFC 2368 and
 *   draft-duerst-mailto-bis-05.txt. Should fully migrate to new specification
 *   when it matures. This requires modifications to the address parser (no
 *   whitespace allowed within the address itself) and UTF-8 support will be
 *   required in the URL.
 */
 
#include "lib.h"
#include "array.h"
#include "str.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "message-date.h"
#include "mail-storage.h"

#include "rfc2822.h"

#include "sieve-ext-enotify.h"
#include "sieve-address.h"
#include "sieve-message.h"

/* To be removed */
#include "sieve-result.h"

/*
 * Configuration
 */
 
#define NTFY_MAILTO_MAX_RECIPIENTS  4
#define NTFY_MAILTO_MAX_HEADERS     16
#define NTFY_MAILTO_MAX_SUBJECT     256

/* 
 * Types 
 */

struct ntfy_mailto_header_field {
	const char *name;
	const char *body;
};

struct ntfy_mailto_recipient {
	const char *full;
	const char *normalized;
	bool carbon_copy;
};

ARRAY_DEFINE_TYPE(recipients, struct ntfy_mailto_recipient);
ARRAY_DEFINE_TYPE(headers, struct ntfy_mailto_header_field);

/* 
 * Mailto notification method
 */
 
static bool ntfy_mailto_compile_check_uri
	(const struct sieve_enotify_log *nlog, const char *uri, const char *uri_body);
static bool ntfy_mailto_compile_check_from
	(const struct sieve_enotify_log *nlog, string_t *from);
static const char *ntfy_mailto_runtime_get_notify_capability
	(const struct sieve_enotify_log *nlog, const char *uri, const char *uri_body, 
		const char *capability);
static bool ntfy_mailto_runtime_check_uri
	(const struct sieve_enotify_log *nlog, const char *uri, const char *uri_body);
static bool ntfy_mailto_runtime_check_operands
	(const struct sieve_enotify_log *nlog, const char *uri,const char *uri_body, 
		string_t *message, string_t *from, pool_t context_pool, 
		void **method_context);
static void ntfy_mailto_action_print
	(const struct sieve_result_print_env *rpenv, 
		const struct sieve_enotify_action *act);	
static bool ntfy_mailto_action_execute
	(const struct sieve_enotify_exec_env *nenv, 
		const struct sieve_enotify_action *act);

const struct sieve_enotify_method mailto_notify = {
	"mailto",
	ntfy_mailto_compile_check_uri,
	NULL,
	ntfy_mailto_compile_check_from,
	NULL,
	ntfy_mailto_runtime_check_uri,
	ntfy_mailto_runtime_get_notify_capability,
	ntfy_mailto_runtime_check_operands,
	NULL,
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
	const char *from_normalized;
};

/*
 * Reserved headers
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

static inline bool _ntfy_mailto_header_unique(const char *field_name)
{
	const char **rhdr = _unique_headers;

	/* Check whether it is supposed to be unique */
	while ( *rhdr != NULL ) {
		if ( strcasecmp(field_name, *rhdr) == 0 )
			return TRUE;
		rhdr++;
	}

	return FALSE;
}

/*
 * Mailto URI parsing
 */
 
/* Util functions */

static inline pool_t array_get_pool_i(struct array *array)
{
	return buffer_get_pool(array->buffer);
}
#define array_get_pool(array) \
	array_get_pool_i(&(array)->arr)

#define _uri_parse_error(LOG, ...) \
	sieve_enotify_error(LOG, "invalid mailto URI: " __VA_ARGS__ )
	
#define _uri_parse_warning(LOG, ...) \
	sieve_enotify_warning(LOG, "mailto URI: " __VA_ARGS__ )

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

static bool _uri_add_valid_recipient
(const struct sieve_enotify_log *nlog, ARRAY_TYPE(recipients) *recipients, 
	string_t *recipient, bool cc)
{
	const char *error;
	const char *normalized;
	 
	/* Verify recipient */
	if ( (normalized=sieve_address_normalize(recipient, &error)) == NULL ) {
		_uri_parse_error(nlog, "invalid recipient '%s': %s",
			str_sanitize(str_c(recipient), 80), error);
		return FALSE;
	}
					
	/* Add recipient to the list */
	if ( recipients != NULL ) {
		struct ntfy_mailto_recipient *new_recipient;
		struct ntfy_mailto_recipient *rcpts;
		unsigned int count, i;
		pool_t pool;

		rcpts = array_get_modifiable(recipients, &count);
		
		/* Enforce limits */
		if ( count >= NTFY_MAILTO_MAX_RECIPIENTS ) {
			if ( count == NTFY_MAILTO_MAX_RECIPIENTS ) {
				_uri_parse_warning(nlog, 
					"more than the maximum %u recipients specified; "
					"rest is discarded", NTFY_MAILTO_MAX_RECIPIENTS);
			}
			return TRUE;	
		}
		
		/* Check for duplicate first */
		for ( i = 0; i < count; i++ ) {
			if ( strcmp(rcpts[i].normalized, normalized) == 0 ) {
				/* Upgrade existing Cc: recipient to a To: recipient if possible */
				rcpts[i].carbon_copy = ( rcpts[i].carbon_copy && cc );
				
				_uri_parse_warning(nlog, "ignored duplicate recipient '%s'",
					str_sanitize(str_c(recipient), 80));
				return TRUE;
			} 
		}			

		/* Add */
		pool = array_get_pool(recipients);
		new_recipient = array_append_space(recipients);
		new_recipient->carbon_copy = cc;
		new_recipient->full = p_strdup(pool, str_c(recipient));
		new_recipient->normalized = p_strdup(pool, normalized);
	}

	return TRUE;
}

static bool _uri_parse_recipients
(const struct sieve_enotify_log *nlog, const char **uri_p, 
	ARRAY_TYPE(recipients) *recipients_r)
{
	string_t *to = t_str_new(128);
	const char *p = *uri_p;
	
	if ( *p == '\0' || *p == '?' )
		return TRUE;
		
	while ( *p != '\0' && *p != '?' ) {
		if ( *p == '%' ) {
			/* % encoded character */
			char ch;
			
			p++;
			
			/* Parse 2-digit hex value */
			if ( !_parse_hex_value(&p, &ch) ) {
				_uri_parse_error(nlog, "invalid %% encoding");
				return FALSE;
			}

			/* Check for delimiter */
			if ( ch == ',' ) {
				/* Verify and add recipient */
				if ( !_uri_add_valid_recipient(nlog, recipients_r, to, FALSE) )
					return FALSE;
			
				/* Reset for next recipient */
				str_truncate(to, 0);
			}	else {
				/* Content character */
				str_append_c(to, ch);
			}
		} else {
			if ( *p == ':' || *p == ';' || !_is_qchar(*p) ) {
				_uri_parse_error
					(nlog, "invalid character '%c' in 'to' part", *p);
				return FALSE;
			}

			/* Content character */
			str_append_c(to, *p);
			p++;
		}
	}	
	
	/* Skip '?' */
	if ( *p != '\0' ) p++;
	
	/* Verify and add recipient */
	if ( !_uri_add_valid_recipient(nlog, recipients_r, to, FALSE) )
		return FALSE;

	*uri_p = p;
	return TRUE;
}

static bool _uri_parse_header_recipients
(const struct sieve_enotify_log *nlog, string_t *rcpt_header, 
	ARRAY_TYPE(recipients) *recipients_r, bool cc)
{
	string_t *to = t_str_new(128);
	const char *p = (const char *) str_data(rcpt_header);
	const char *pend = p + str_len(rcpt_header);
		
	while ( p < pend ) {
		if ( *p == ',' ) {
			/* Verify and add recipient */
			if ( !_uri_add_valid_recipient(nlog, recipients_r, to, cc) )
				return FALSE;
			
			/* Reset for next recipient */
			str_truncate(to, 0);
		} else {
			/* Content character */
			str_append_c(to, *p);
		}
		p++;
	}	
	
	/* Verify and add recipient */
	if ( !_uri_add_valid_recipient(nlog, recipients_r, to, cc) )
		return FALSE;

	return TRUE;	
}

static bool _uri_header_is_duplicate
(ARRAY_TYPE(headers) *headers, const char *field_name)
{	
	if ( _ntfy_mailto_header_unique(field_name) ) {
		const struct ntfy_mailto_header_field *hdrs;
		unsigned int count, i;

		hdrs = array_get(headers, &count);	
		for ( i = 0; i < count; i++ ) {
			if ( strcasecmp(hdrs[i].name, field_name) == 0 ) 
				return TRUE;
		}
	}
	
	return FALSE;
}

static bool _uri_parse_headers
(const struct sieve_enotify_log *nlog, const char **uri_p, 
	ARRAY_TYPE(headers) *headers_r, ARRAY_TYPE(recipients) *recipients_r,
	const char **body, const char **subject)
{
	unsigned int header_count = 0;
	string_t *field = t_str_new(128);
	const char *p = *uri_p;
	pool_t pool = NULL;

	if ( body != NULL )
		*body = NULL;
		
	if ( subject != NULL )
		*subject = NULL;
			
	if ( headers_r != NULL )
		pool = array_get_pool(headers_r);
		
	while ( *p != '\0' ) {
		enum {
			_HNAME_IGNORED, 
			_HNAME_GENERIC,
			_HNAME_TO,
			_HNAME_CC,
			_HNAME_SUBJECT, 
			_HNAME_BODY 
		} hname_type = _HNAME_GENERIC;
		struct ntfy_mailto_header_field *hdrf = NULL;
		const char *field_name;
		
		/* Parse field name */
		while ( *p != '\0' && *p != '=' ) {
			char ch = *p;
			p++;
			
			if ( ch == '%' ) {
				/* Encoded, parse 2-digit hex value */
				if ( !_parse_hex_value(&p, &ch) ) {
					_uri_parse_error(nlog, "invalid %% encoding");
					return FALSE;
				}
			} else if ( ch != '=' && !_is_qchar(ch) ) {
				_uri_parse_error
					(nlog, "invalid character '%c' in header field name part", ch);
				return FALSE;
			}

			str_append_c(field, ch);
		}
		if ( *p != '\0' ) p++;

		/* Verify field name */
		if ( !rfc2822_header_field_name_verify(str_c(field), str_len(field)) ) {
			_uri_parse_error(nlog, "invalid header field name");
			return FALSE;
		}

		if ( header_count >= NTFY_MAILTO_MAX_HEADERS ) {
			/* Refuse to accept more headers than allowed by policy */
			if ( header_count == NTFY_MAILTO_MAX_HEADERS ) {
				_uri_parse_warning(nlog, "more than the maximum %u headers specified; "
					"rest is discarded", NTFY_MAILTO_MAX_HEADERS);
			}
			
			hname_type = _HNAME_IGNORED;
		} else {
			/* Add new header field to array and assign its name */
			
			field_name = str_c(field);
			if ( strcasecmp(field_name, "to") == 0 )
				hname_type = _HNAME_TO;
			else if ( strcasecmp(field_name, "cc") == 0 )
				hname_type = _HNAME_CC;
			else if ( strcasecmp(field_name, "subject") == 0 )
				hname_type = _HNAME_SUBJECT;
			else if ( strcasecmp(field_name, "body") == 0 )
				hname_type = _HNAME_BODY;
			else if ( _ntfy_mailto_header_allowed(field_name) ) {
				if ( headers_r != NULL ) {
					if ( !_uri_header_is_duplicate(headers_r, field_name) ) {
						hdrf = array_append_space(headers_r);
						hdrf->name = p_strdup(pool, field_name);
					} else {
						_uri_parse_warning(nlog, 
							"ignored duplicate for unique header field '%s'",
							str_sanitize(field_name, 32));
						hname_type = _HNAME_IGNORED;
					}
				} else {
					hname_type = _HNAME_IGNORED;
				}
			} else {
				_uri_parse_warning(nlog, "ignored reserved header field '%s'",
					str_sanitize(field_name, 32));
				hname_type = _HNAME_IGNORED;
			}
		}
		
		header_count++;
			
		/* Reset for field body */
		str_truncate(field, 0);
		
		/* Parse field body */		
		while ( *p != '\0' && *p != '&' ) {
			char ch = *p;
			p++;
			
			if ( ch == '%' ) {
				/* Encoded, parse 2-digit hex value */
				if ( !_parse_hex_value(&p, &ch) ) {
					_uri_parse_error(nlog, "invalid %% encoding");
					return FALSE;
				}
			} else if ( ch != '=' && !_is_qchar(ch) ) {
				_uri_parse_error
					(nlog, "invalid character '%c' in header field value part", ch);
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
				_uri_parse_error
					(nlog, "invalid header field body");
				return FALSE;
			}
		}
		
		/* Assign field body */

		switch ( hname_type ) {
		case _HNAME_IGNORED:
			break;
		case _HNAME_TO:
			/* Gracefully allow duplicate To fields */
			if ( !_uri_parse_header_recipients(nlog, field, recipients_r, FALSE) )
				return FALSE;
			break;
		case _HNAME_CC:
			/* Gracefully allow duplicate Cc fields */
			if ( !_uri_parse_header_recipients(nlog, field, recipients_r, TRUE) )
				return FALSE;
			break;
		case _HNAME_SUBJECT:
			if ( subject != NULL ) {
				/* Igore duplicate subject field */
				if ( *subject == NULL )
					*subject = p_strdup(pool, str_c(field));
				else
					_uri_parse_warning(nlog, "ignored duplicate subject field");
			}
			break;
		case _HNAME_BODY:
			if ( body != NULL ) {
				/* Igore duplicate body field */
				if ( *body == NULL )
					*body = p_strdup(pool, str_c(field));
				else 
					_uri_parse_warning(nlog, "ignored duplicate body field");
			}				
			break;
		case _HNAME_GENERIC:
			if ( hdrf != NULL ) 
				hdrf->body = p_strdup(pool, str_c(field));
			break;
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
(const struct sieve_enotify_log *nlog, const char *uri_body, 
	ARRAY_TYPE(recipients) *recipients_r, ARRAY_TYPE(headers) *headers_r,
	const char **body, const char **subject)
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
	 * to         ~= *tqchar
	 * tqchar     ~= <qchar> without ";" and ":" 
	 * 
	 * Scheme 'mailto:' already parsed, starting parse after colon
	 */

	/* First extract to-part by searching for '?' and decoding % items
	 */

	if ( !_uri_parse_recipients(nlog, &p, recipients_r) )
		return FALSE;	

	/* Extract hfield items */	
	
	while ( *p != '\0' ) {		
		/* Extract hfield item by searching for '&' and decoding '%' items */
		if ( !_uri_parse_headers(nlog, &p, headers_r, recipients_r, body, subject) )
			return FALSE;		
	}
	
	return TRUE;
}

/*
 * Validation
 */

static bool ntfy_mailto_compile_check_uri
(const struct sieve_enotify_log *nlog, const char *uri ATTR_UNUSED,
	const char *uri_body)
{	
	ARRAY_TYPE(recipients) recipients;
	ARRAY_TYPE(headers) headers;
	const char *body = NULL, *subject = NULL;

	t_array_init(&recipients, NTFY_MAILTO_MAX_RECIPIENTS);
	t_array_init(&headers, NTFY_MAILTO_MAX_HEADERS);
	
	if ( !ntfy_mailto_parse_uri
		(nlog, uri_body, &recipients, &headers, &body, &subject) )
		return FALSE;
		
	if ( array_count(&recipients) == 0 )
		sieve_enotify_warning(nlog, "notification URI specifies no recipients");
	
	return TRUE;
}

static bool ntfy_mailto_compile_check_from
(const struct sieve_enotify_log *nlog, string_t *from)
{
	const char *error;
	bool result = FALSE;

	T_BEGIN {
		result = sieve_address_validate(from, &error);

		if ( !result ) {
			sieve_enotify_error(nlog,
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
 
static const char *ntfy_mailto_runtime_get_notify_capability
(const struct sieve_enotify_log *nlog ATTR_UNUSED, const char *uri ATTR_UNUSED, 
	const char *uri_body, const char *capability)
{
	if ( !ntfy_mailto_parse_uri(NULL, uri_body, NULL, NULL, NULL, NULL) ) {
		return NULL;
	}
	
	if ( strcasecmp(capability, "online") == 0 ) 
		return "maybe";
	
	return NULL;
}

static bool ntfy_mailto_runtime_check_uri
(const struct sieve_enotify_log *nlog ATTR_UNUSED, const char *uri ATTR_UNUSED,
	const char *uri_body)
{
	return ntfy_mailto_parse_uri(NULL, uri_body, NULL, NULL, NULL, NULL);
}
 
static bool ntfy_mailto_runtime_check_operands
(const struct sieve_enotify_log *nlog, const char *uri ATTR_UNUSED,
	const char *uri_body, string_t *message ATTR_UNUSED, string_t *from, 
	pool_t context_pool, void **method_context)
{
	struct ntfy_mailto_context *mtctx;
	const char *error, *normalized;

	/* Need to create context before validation to have arrays present */
	mtctx = p_new(context_pool, struct ntfy_mailto_context, 1);

	/* Validate :from */
	if ( from != NULL ) {
		T_BEGIN {
			normalized = sieve_address_normalize(from, &error);

			if ( normalized == NULL ) {
				sieve_enotify_error(nlog,
					"specified :from address '%s' is invalid for "
					"the mailto method: %s",
					str_sanitize(str_c(from), 128), error);
			} else 
				mtctx->from_normalized = p_strdup(context_pool, normalized);
		} T_END;

		if ( !normalized ) return FALSE;
	}

	p_array_init(&mtctx->recipients, context_pool, NTFY_MAILTO_MAX_RECIPIENTS);
	p_array_init(&mtctx->headers, context_pool, NTFY_MAILTO_MAX_HEADERS);

	if ( !ntfy_mailto_parse_uri
		(nlog, uri_body, &mtctx->recipients, &mtctx->headers, &mtctx->body, 
			&mtctx->subject) ) {
		return FALSE;
	}

	*method_context = (void *) mtctx;
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
	const struct ntfy_mailto_recipient *recipients;
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
	if ( count == 0 ) {
		sieve_result_printf(rpenv,   "       NONE, action has no effect\n");
	} else {
		for ( i = 0; i < count; i++ ) {
			if ( recipients[i].carbon_copy )
				sieve_result_printf(rpenv,   "       + Cc: %s\n", recipients[i].full);
			else
				sieve_result_printf(rpenv,   "       + To: %s\n", recipients[i].full);
		}
	}
	
	headers = array_get(&mtctx->headers, &count);
	if ( count > 0 ) {
		sieve_result_printf(rpenv,   "    => headers      :\n" );	
		for ( i = 0; i < count; i++ ) {
			sieve_result_printf(rpenv,   "       + %s: %s\n", 
				headers[i].name, headers[i].body);
		}
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
	const struct sieve_enotify_log *nlog = nenv->notify_log;
	const struct sieve_message_data *msgdata = nenv->msgdata;
	const struct sieve_script_env *senv = nenv->scriptenv;
	struct ntfy_mailto_context *mtctx = 
		(struct ntfy_mailto_context *) act->method_context;	
	const char *from = NULL; 
	const char *subject = mtctx->subject;
	const char *body = mtctx->body;
	string_t *to, *cc;
	const struct ntfy_mailto_recipient *recipients;
	void *smtp_handle;
	unsigned int count, i;
	FILE *f;
	const char *outmsgid;

	/* Get recipients */
	recipients = array_get(&mtctx->recipients, &count);
	if ( count == 0  ) {
		sieve_enotify_warning(nlog, 
			"notify mailto uri specifies no recipients; action has no effect");
		return TRUE;
	}

	/* Just to be sure */
	if ( senv->smtp_open == NULL || senv->smtp_close == NULL ) {
		sieve_enotify_warning(nlog, 
			"notify mailto method has no means to send mail");
		return TRUE;
	}
	
	/* Determine from address */
	if ( msgdata->return_path != NULL ) {
		if ( act->from == NULL )
			from = t_strdup_printf("Postmaster <%s>", senv->postmaster_address);
		else
			/* FIXME: validate */
			from = act->from;
	}
	
	/* Determine subject */
	if ( act->message != NULL ) {
		/* FIXME: handle UTF-8 */
		subject = str_sanitize(act->message, NTFY_MAILTO_MAX_SUBJECT);
	} else if ( subject == NULL ) {
		const char *const *hsubject;
		
		/* Fetch subject from original message */
		if ( mail_get_headers_utf8
			(msgdata->mail, "subject", &hsubject) >= 0 )
			subject = str_sanitize(t_strdup_printf("Notification: %s", hsubject[0]), 
				NTFY_MAILTO_MAX_SUBJECT);
		else
			subject = "Notification: (no subject)";
	}

	/* Compose To and Cc headers */
	to = NULL;
	cc = NULL;
	for ( i = 0; i < count; i++ ) {
		if ( recipients[i].carbon_copy ) {
			if ( cc == NULL ) {
				cc = t_str_new(256);
				str_append(cc, recipients[i].full);
			} else {
				str_append(cc, ", ");
				str_append(cc, recipients[i].full);
			}
		} else {
			if ( to == NULL ) {
				to = t_str_new(256);
				str_append(to, recipients[i].full);
			} else {
				str_append(to, ", ");
				str_append(to, recipients[i].full);
			}
		}
	}

	/* Send message to all recipients */
	for ( i = 0; i < count; i++ ) {
		const struct ntfy_mailto_header_field *headers;
		unsigned int h, hcount;

		smtp_handle = senv->smtp_open(recipients[i].normalized, from, &f);
		outmsgid = sieve_message_get_new_id(senv);
	
		rfc2822_header_field_write(f, "X-Sieve", SIEVE_IMPLEMENTATION);
		rfc2822_header_field_write(f, "Message-ID", outmsgid);
		rfc2822_header_field_write(f, "Date", message_date_create(ioloop_time));
		rfc2822_header_field_write(f, "Subject", subject);

		rfc2822_header_field_printf(f, "From", "%s", from);

		if ( to != NULL )
			rfc2822_header_field_printf(f, "To", "%s", str_c(to));
		
		if ( cc != NULL )
			rfc2822_header_field_printf(f, "Cc", "%s", str_c(cc));
			
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
			sieve_enotify_log(nlog, 
				"sent mail notification to <%s>", 
				str_sanitize(recipients[i].normalized, 80));
		} else {
			sieve_enotify_error(nlog,
				"failed to send mail notification to <%s> "
				"(refer to system log for more information)", 
				str_sanitize(recipients[i].normalized, 80));
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
				sieve_enotify_log(nenv->notify_log, 
					"not sending notification for auto-submitted message from <%s>", 
					str_sanitize(msgdata->return_path, 128));	
					return TRUE;				 
			}
			hdsp++;
		}
	}

	return ntfy_mailto_send(nenv, act);
}




