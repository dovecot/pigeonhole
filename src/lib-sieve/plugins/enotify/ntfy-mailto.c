/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */
 
#include "lib.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "message-date.h"

#include "sieve-common.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-actions.h"
#include "sieve-result.h"

#include "sieve-ext-enotify.h"

/* 
 * Mailto notification method
 */
static bool ntfy_mailto_validate_uri
	(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
		const char *uri_body);
static bool ntfy_mailto_execute
	(const struct sieve_action_exec_env *aenv, 
		const struct sieve_enotify_context *nctx);

const struct sieve_enotify_method mailto_notify = {
	"mailto",
	ntfy_mailto_validate_uri,
	ntfy_mailto_execute
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
		return (int) digit - 'a' + 0x0A;
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
	return TRUE;	
}

static bool uri_extract_part
(const char **uri_p, char delim, string_t *part, const char **error_r)
{
	const char *p = *uri_p;
	
	while ( *p != '\0' && *p != delim ) {
		if ( *p == '%' ) {
			char ch;
			
			p++;
			
			if ( !_parse_hex_value(&p, &ch) ) {
				*error_r = "invalid % encoding";
				return FALSE;
			}
			
			str_append_c(part, ch);
		} else {
			str_append_c(part, *p);
			p++;
		}
	}
	
	p++;
	*uri_p = p;
	return TRUE;
}

static bool ntfy_mailto_parse_uri
(const char *uri_body, const char ***recipients_r, const char ***headers_r,
	const char **error_r)
{
	string_t *to = t_str_new(128);
	string_t *hfield = t_str_new(128);

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

	if ( !uri_extract_part(&p, '?', to, error_r) )
		return FALSE;	

	/* Parse to part */
	
	// ....
	
	/* Extract hfield items */	
	
	while ( *p != '\0' ) {		
		/* Extract hfield item by searching for '&' and decoding '%' items */
		if ( !uri_extract_part(&p, '&', hfield, error_r) )
			return FALSE;		
			
		/* Add header to list */
	
		// ....
		
		/* Reset for next header */
		str_truncate(hfield, 0);
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
			"failed to send mail notification to <%s> "
			"(refer to system log for more information)", 
			str_sanitize(recipient, 80));
	}

	return TRUE;
}
