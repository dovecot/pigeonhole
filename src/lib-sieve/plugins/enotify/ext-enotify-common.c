/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */
 
#include "lib.h"
#include "str.h"
#include "str-sanitize.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-ast.h"
#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "ext-enotify-limits.h"
#include "ext-enotify-common.h"

#include <ctype.h>

/* FIXME: (from draft RFC)
 *
 * Header/envelope tests [Sieve] together with Sieve variables can be
 * used to extract the list of users to receive notifications from the
 * incoming email message or its envelope.  This is potentially quite
 * dangerous, as this can be used for Deny Of Service attacks on
 * recipients controlled by the message sender.  For this reason
 * implementations SHOULD NOT allow use of variables containing values
 * extracted from the email message in the method parameter to the
 * notify action.  Note that violation of this SHOULD NOT may result in
 * the creation of an open relay, i.e. any sender would be able to
 * create specially crafted email messages that would result in
 * notifications delivered to recipients under the control of the
 * sender.  In worst case this might result in financial loss by user
 * controlling the Sieve script and/or by recipients of notifications
 * (e.g. if a notification is an SMS message).
 *
 * --> This is currently not possible to check.
 */

/*
 * Notify capability
 */

static const char *ext_notify_get_methods_string(void);

const struct sieve_extension_capabilities notify_capabilities = {
	"notify",
	&enotify_extension,
	ext_notify_get_methods_string
};

/*
 * Notify method registry
 */
 
static ARRAY_DEFINE(ext_enotify_methods, const struct sieve_enotify_method *); 

void ext_enotify_methods_init(void)
{
	p_array_init(&ext_enotify_methods, default_pool, 4);

	sieve_enotify_method_register(&mailto_notify);
}

void ext_enotify_methods_deinit(void)
{
	array_free(&ext_enotify_methods);
}

void sieve_enotify_method_register(const struct sieve_enotify_method *method) 
{
	array_append(&ext_enotify_methods, &method, 1);
}

const struct sieve_enotify_method *ext_enotify_method_find
(const char *identifier) 
{
	unsigned int meth_count, i;
	const struct sieve_enotify_method *const *methods;
	 
	methods = array_get(&ext_enotify_methods, &meth_count);
		
	for ( i = 0; i < meth_count; i++ ) {
		if ( strcasecmp(methods[i]->identifier, identifier) == 0 ) {
			return methods[i];
		}
	}
	
	return NULL;
}

static const char *ext_notify_get_methods_string(void)
{
	unsigned int meth_count, i;
	const struct sieve_enotify_method *const *methods;
	string_t *result = t_str_new(128);
	 
	methods = array_get(&ext_enotify_methods, &meth_count);
		
	if ( meth_count > 0 ) {
		str_append(result, methods[0]->identifier);
		
		for ( i = 1; i < meth_count; i++ ) {
			str_append_c(result, ' ');
			str_append(result, methods[i]->identifier);
		}
		
		return str_c(result);
	}
	
	return NULL;
}

/*
 * Compile-time argument validation
 */
 
static const char *ext_enotify_uri_scheme_parse(const char **uri_p)
{
	string_t *scheme = t_str_new(EXT_ENOTIFY_MAX_SCHEME_LEN);
	const char *p = *uri_p;
	unsigned int len = 0;
	
	/* RFC 3968:
	 *
	 *   scheme  = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
	 *
	 * FIXME: we do not allow '%' in schemes. Is this correct?
	 */
	 
	if ( !i_isalpha(*p) )
		return NULL;
		
	str_append_c(scheme, *p);
	p++;
		
	while ( *p != '\0' && len < EXT_ENOTIFY_MAX_SCHEME_LEN ) {
			
		if ( !i_isalnum(*p) && *p != '+' && *p != '-' && *p != '.' )
			break;
	
		str_append_c(scheme, *p);
		p++;
		len++;
	}
	
	if ( *p != ':' )
		return NULL;
	p++;
	
	*uri_p = p;
	return str_c(scheme);
}

static bool ext_enotify_option_parse
(struct sieve_enotify_log *nlog, const char *option, bool name_only,
	const char **opt_name_r, const char **opt_value_r)
{
	const char *p = option;
	
	/* "<optionname>=<value>".
	 * 
	 * l-d = ALPHA / DIGIT
	 * l-d-p = l-d / "." / "-" / "_"
	 * optionname = l-d *l-d-p
	 * value = *(%x01-09 / %x0B-0C / %x0E-FF)
	 */
				
	/* 
	 * Parse option name 
	 *
	 * optionname = l-d *l-d-p
	 */
	
	/* Explicitly report empty option as such */
	if ( *p == '\0' ) {
		sieve_enotify_error(nlog, "empty option specified");
		return FALSE;
	}

	/* l-d = ALPHA / DIGIT */
	if ( i_isalnum(*p) ) {
		p++;
	
		/* l-d-p = l-d / "." / "-" / "_" */
		while ( i_isalnum(*p) || *p == '.' || *p == '-' || *p == '_' )
			p++;
	}
	
	/* Parsing must end at '=' and we must parse at least one character */
	if ( *p != '=' || p == option ) {
		sieve_enotify_error(nlog, "invalid option name specified in option '%s'",
				str_sanitize(option, 80));
		return FALSE;
	}
	
	/* Assign option name */
	if ( opt_name_r != NULL ) 
		*opt_name_r = t_strdup_until(option, p);
	
	/* Skip '=' */
	p++;
	
	/* Exit now if only the option name is of interest */
	if ( name_only )
		return TRUE;
			
	/* 
	 * Parse option value
	 */
	 
	/* value = *(%x01-09 / %x0B-0C / %x0E-FF) */
	while ( *p != '\0' && *p != 0x0A && *p != 0x0D )
		p++;
		
	/* Parse must end at end of string */
	if ( *p != '\0' ) {
		sieve_enotify_error(nlog, 
			"notify command: invalid option value specified in option '%s'",
				str_sanitize(option, 80));
		return FALSE;
	}
	
	/* Assign option value */
	if ( opt_value_r != NULL )
		*opt_value_r = p;
		
	return TRUE;
} 

struct _ext_enotify_option_check_context {
	struct sieve_validator *valdtr;
	const struct sieve_enotify_method *method;
};

static int _ext_enotify_option_check
(void *context, struct sieve_ast_argument *arg)
{
	struct _ext_enotify_option_check_context *optn_context = 
		(struct _ext_enotify_option_check_context *) context;
	struct sieve_validator *valdtr = optn_context->valdtr;
	const struct sieve_enotify_method *method = optn_context->method;
	struct sieve_enotify_log nlog;
	const char *option = sieve_ast_argument_strc(arg);
	const char *opt_name = NULL, *opt_value = NULL;
	bool literal = sieve_argument_is_string_literal(arg);
	
	/* Compose log structure */
	memset(&nlog, 0, sizeof(nlog));
	nlog.ehandler = sieve_validator_error_handler(valdtr);
	nlog.prefix = "notify command";
	nlog.location = sieve_error_script_location
		(sieve_validator_script(valdtr), arg->source_line);
		
	/* Parse option */
	if ( !literal ) {
		/* Variable string: partial option parse
		 * 
		 * If the string item is not a string literal, it cannot be validated fully
		 * at compile time. We can however check whether the '=' is in the string
		 * specification and whether the part before the '=' is a valid option name.
		 * In that case, the method option check function is called with the value
		 * parameter equal to NULL, meaning that it should only check the validity
		 * of the option itself and not the assigned value.
		 */ 
		if ( !ext_enotify_option_parse(NULL, option, TRUE, &opt_name, &opt_value) )
			return TRUE;
	} else {
		/* Literal string: full option parse */
		if ( !ext_enotify_option_parse
			(&nlog, option, FALSE, &opt_name, &opt_value) )
			return FALSE;
	}
	
	/* Call method's option check function */
	if ( method->compile_check_option != NULL ) 
		return method->compile_check_option(&nlog, opt_name, opt_value); 
	
	return TRUE;
}

bool ext_enotify_compile_check_arguments
(struct sieve_validator *valdtr, struct sieve_ast_argument *uri_arg,
	struct sieve_ast_argument *msg_arg, struct sieve_ast_argument *from_arg,
	struct sieve_ast_argument *options_arg)
{
	const char *uri = sieve_ast_argument_strc(uri_arg);
	const char *scheme;
	const struct sieve_enotify_method *method;
	struct sieve_enotify_log nlog;

	/* If the uri string is not a constant literal, we cannot determine which
	 * method is used, so we bail out successfully and defer checking to runtime.
	 */
	if ( !sieve_argument_is_string_literal(uri_arg) )
		return TRUE;
	
	/* Parse scheme part of URI */
	if ( (scheme=ext_enotify_uri_scheme_parse(&uri)) == NULL ) {
		sieve_argument_validate_error(valdtr, uri_arg, 
			"notify command: invalid scheme part for method URI '%s'", 
			str_sanitize(sieve_ast_argument_strc(uri_arg), 80));
		return FALSE;
	}
	
	/* Find used method with the parsed scheme identifier */
	if ( (method=ext_enotify_method_find(scheme)) == NULL ) {
		sieve_argument_validate_error(valdtr, uri_arg, 
			"notify command: invalid method '%s'", scheme);
		return FALSE;
	}

	/* Compose log structure */
	memset(&nlog, 0, sizeof(nlog));
	nlog.ehandler = sieve_validator_error_handler(valdtr);
	nlog.prefix = "notify command";
	
	/* Check URI itself */
	if ( method->compile_check_uri != NULL ) {
		/* Set log location to location of URI argument */
		nlog.location = sieve_error_script_location
			(sieve_validator_script(valdtr), uri_arg->source_line);

		/* Execute method check function */
		if ( !method->compile_check_uri
			(&nlog, sieve_ast_argument_strc(uri_arg), uri) )
			return FALSE;
	}

	/* Check :message argument */
	if ( msg_arg != NULL && sieve_argument_is_string_literal(msg_arg) && 
		method->compile_check_message != NULL ) {
		/* Set log location to location of :message argument */
		nlog.location = sieve_error_script_location
			(sieve_validator_script(valdtr), msg_arg->source_line);

		/* Execute method check function */
		if ( !method->compile_check_message
			(&nlog, sieve_ast_argument_str(msg_arg)) )
			return FALSE;
	}

	/* Check :from argument */
	if ( from_arg != NULL && sieve_argument_is_string_literal(from_arg) &&
		method->compile_check_from != NULL ) {
		/* Set log location to location of :from argument */
		nlog.location = sieve_error_script_location
			(sieve_validator_script(valdtr), from_arg->source_line);

		/* Execute method check function */
		if ( !method->compile_check_from(&nlog, sieve_ast_argument_str(from_arg)) )
			return FALSE;
	}
	
	/* Check :options argument */
	if ( options_arg != NULL ) {
		struct sieve_ast_argument *option = options_arg;
		struct _ext_enotify_option_check_context optn_context = { valdtr, method };
		
		/* Parse and check options */
		if ( sieve_ast_stringlist_map
			(&option, (void *) &optn_context, _ext_enotify_option_check) <= 0 )
			return FALSE;
			
		/* Discard argument if options are not accepted by method */
		if ( method->compile_check_option == NULL ) {
			sieve_argument_validate_warning(valdtr, options_arg, 
				"notify command: method '%s' accepts no options", scheme);
			(void)sieve_ast_arguments_detach(options_arg,1);
		}
	}
	
	return TRUE;
}

/*
 * Runtime operand checking
 */
 
bool ext_enotify_runtime_method_validate
(const struct sieve_runtime_env *renv, unsigned int source_line,
	string_t *method_uri)
{
	const struct sieve_enotify_method *method;
	const char *uri = str_c(method_uri);
	const char *scheme;
	
	/* Get the method */
	
	if ( (scheme=ext_enotify_uri_scheme_parse(&uri)) == NULL )
		return FALSE;
	
	if ( (method=ext_enotify_method_find(scheme)) == NULL )
		return FALSE;
		
	/* Validate the provided URI */
	
	if ( method->runtime_check_uri != NULL ) {
		struct sieve_enotify_log nlog;
		
		memset(&nlog, 0, sizeof(nlog));
		nlog.location = sieve_error_script_location(renv->script, source_line);
		nlog.ehandler = sieve_interpreter_get_error_handler(renv->interp);
		nlog.prefix = "valid_notify_method test";

		/* Use the method check function to validate the URI */
		return method->runtime_check_uri(&nlog, str_c(method_uri), uri);
	}

	/* Method has no check function */
	return TRUE;
}
 
static const struct sieve_enotify_method *ext_enotify_get_method
(const struct sieve_runtime_env *renv, unsigned int source_line,
	string_t *method_uri, const char **uri_body_r)
{
	const struct sieve_enotify_method *method;
	const char *uri = str_c(method_uri);
	const char *scheme;
	
	/* Parse part before ':' of the uri (the scheme) and use it to identify
	 * notify method.
	 */
	if ( (scheme=ext_enotify_uri_scheme_parse(&uri)) == NULL ) {
		sieve_runtime_error
			(renv, sieve_error_script_location(renv->script, source_line),
				"invalid scheme part for method URI '%s'", 
				str_sanitize(str_c(method_uri), 80));
		return NULL;
	}
	
	/* Find the notify method */
	if ( (method=ext_enotify_method_find(scheme)) == NULL ) {
		sieve_runtime_error
			(renv, sieve_error_script_location(renv->script, source_line),
				"invalid notify method '%s'", scheme);
		return NULL;
	}

	/* Return the parse pointer and the found method */
	*uri_body_r = uri;
	return method;
}

const char *ext_enotify_runtime_get_method_capability
(const struct sieve_runtime_env *renv, unsigned int source_line,
	string_t *method_uri, const char *capability)
{
	const struct sieve_enotify_method *method;
	const char *uri;
	
	/* Get method */
	method = ext_enotify_get_method(renv, source_line, method_uri, &uri);
	if ( method == NULL ) return NULL;
	
	/* Get requested capability */
	if ( method->runtime_get_method_capability != NULL ) {
		struct sieve_enotify_log nlog;
		
		/* Compose log structure */
		memset(&nlog, 0, sizeof(nlog));
		nlog.location = sieve_error_script_location(renv->script, source_line);
		nlog.ehandler = sieve_interpreter_get_error_handler(renv->interp);
		nlog.prefix = "notify_method_capability test";

		/* Execute method function to acquire capability value */
		return method->runtime_get_method_capability
			(&nlog, str_c(method_uri), uri, capability);
	}

	return NULL;
}

int ext_enotify_runtime_check_operands
(const struct sieve_runtime_env *renv, unsigned int source_line,
	string_t *method_uri, string_t *message, string_t *from, 
	struct sieve_coded_stringlist *options, 
	const struct sieve_enotify_method **method_r, void **method_context)
{
	const struct sieve_enotify_method *method;
	const char *uri;
	
	/* Get method */
	method = ext_enotify_get_method(renv, source_line, method_uri, &uri);
	if ( method == NULL ) return SIEVE_EXEC_FAILURE;
	
	/* Check provided operands */
	if ( method->runtime_check_operands != NULL ) {
		struct sieve_enotify_log nlog;
		
		/* Compose log structure */
		memset(&nlog, 0, sizeof(nlog));
		nlog.location = sieve_error_script_location(renv->script, source_line);
		nlog.ehandler = sieve_interpreter_get_error_handler(renv->interp);
		nlog.prefix = "notify action";

		/* Execute check function */
		if ( method->runtime_check_operands(&nlog, str_c(method_uri), uri, message, 
			from, sieve_result_pool(renv->result), method_context) ) {
			
			/* Check any provided options */
			if ( options != NULL ) {			
				int result = TRUE;
				string_t *option = NULL;
			
				/* Iterate through all provided options */
				while ( result && 
					(result=sieve_coded_stringlist_next_item(options, &option)) && 
					option != NULL ) {
					const char *opt_name = NULL, *opt_value = NULL;
				
					/* Parse option into <optionname> and <value> */
					if ( ext_enotify_option_parse
						(&nlog, str_c(option), FALSE, &opt_name, &opt_value) ) {
					
						/* Set option */
						if ( method->runtime_set_option != NULL ) {
							(void) method->runtime_set_option
								(&nlog, *method_context, opt_name, opt_value);
						}
					}
				}
			
				/* Check for binary corruptions encountered during string list iteration
				 */
				if ( result ) {
					*method_r = method;
					return SIEVE_EXEC_OK;
				}
	
				/* Binary corrupt */
				sieve_runtime_trace_error(renv, "invalid item in options string list");
				return SIEVE_EXEC_BIN_CORRUPT;
			}

			/* No options */			
			*method_r = method;
			return SIEVE_EXEC_OK;
		}
		
		/* Check failed */
		return SIEVE_EXEC_FAILURE;
	}

	/* No check function defined: a most unlikely situation */
	*method_context = NULL;	
	*method_r = method;
	return SIEVE_EXEC_OK;
}

/*
 * Method logging
 */

void sieve_enotify_error
(const struct sieve_enotify_log *nlog, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	
	if ( nlog == NULL ) return;
	
	T_BEGIN {
		if ( nlog->prefix == NULL )
			sieve_verror(nlog->ehandler, nlog->location, fmt, args);
		else
			sieve_error(nlog->ehandler, nlog->location, "%s: %s", nlog->prefix, 
				t_strdup_vprintf(fmt, args));
	} T_END;
	
	va_end(args);
}

void sieve_enotify_warning
(const struct sieve_enotify_log *nlog, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	
	if ( nlog == NULL ) return;
	
	T_BEGIN { 
		if ( nlog->prefix == NULL )
			sieve_vwarning(nlog->ehandler, nlog->location, fmt, args);
		else			
			sieve_warning(nlog->ehandler, nlog->location, "%s: %s", nlog->prefix, 
				t_strdup_vprintf(fmt, args));
	} T_END;
	
	va_end(args);
}

void sieve_enotify_log
(const struct sieve_enotify_log *nlog, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);

	if ( nlog == NULL ) return;
	
	T_BEGIN { 
		if ( nlog->prefix == NULL )
			sieve_vinfo(nlog->ehandler, nlog->location, fmt, args);
		else
			sieve_info(nlog->ehandler, nlog->location, "%s: %s", nlog->prefix, 
				t_strdup_vprintf(fmt, args));	
	} T_END;
	
	va_end(args);
}


