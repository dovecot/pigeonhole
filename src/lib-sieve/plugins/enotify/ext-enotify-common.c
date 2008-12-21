/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */
 
#include "lib.h"
#include "str.h"
#include "str-sanitize.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "ext-enotify-limits.h"
#include "ext-enotify-common.h"

#include <ctype.h>

/*
 * Notify capability
 */

static const char *ext_notify_get_methods_string(void);

const struct sieve_extension_capabilities notify_capabilities = {
	"notify",
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

bool ext_enotify_uri_validate
	(struct sieve_validator *valdtr, struct sieve_ast_argument *arg)
{
	const char *uri = sieve_ast_argument_strc(arg);
	const char *scheme;
	const struct sieve_enotify_method *method;
	
	if ( (scheme=ext_enotify_uri_scheme_parse(&uri)) == NULL ) {
		sieve_argument_validate_error(valdtr, arg, 
			"notify command: invalid scheme part for method URI '%s'", 
			str_sanitize(sieve_ast_argument_strc(arg), 80));
		return FALSE;
	}
	
	if ( (method=ext_enotify_method_find(scheme)) == NULL ) {
		sieve_argument_validate_error(valdtr, arg, 
			"notify command: invalid method '%s'", scheme);
		return FALSE;
	}
	
	if ( method->validate_uri != NULL ) {
		struct sieve_enotify_log_context nlctx;
		
		memset(&nlctx, 0, sizeof(nlctx));
		nlctx.location = sieve_error_script_location
			(sieve_validator_script(valdtr), arg->source_line);
		nlctx.ehandler = sieve_validator_error_handler(valdtr);
		nlctx.prefix = "notify command";

		return method->validate_uri(&nlctx, sieve_ast_argument_strc(arg), uri);
	}
	
	return TRUE;
}

/*
 * Runtime operand checking
 */

const struct sieve_enotify_method *ext_enotify_runtime_check_operands
(const struct sieve_runtime_env *renv, unsigned int source_line,
	const char *method_uri,	const char *message, const char *from, void **context)
{
	const char *uri = method_uri;
	const char *scheme;
	const struct sieve_enotify_method *method;
	
	if ( (scheme=ext_enotify_uri_scheme_parse(&uri)) == NULL ) {
		sieve_runtime_error
			(renv, sieve_error_script_location(renv->script, source_line),
				"invalid scheme part for method URI '%s'", 
				str_sanitize(method_uri, 80));
		return NULL;
	}
	
	if ( (method=ext_enotify_method_find(scheme)) == NULL ) {
		sieve_runtime_error
			(renv, sieve_error_script_location(renv->script, source_line),
				"invalid notify method '%s'", scheme);
		return NULL;
	}

	if ( method->runtime_check_operands != NULL ) {
		struct sieve_enotify_log_context nlctx;
		
		memset(&nlctx, 0, sizeof(nlctx));
		nlctx.location = sieve_error_script_location(renv->script, source_line);
		nlctx.ehandler = sieve_interpreter_get_error_handler(renv->interp);
		nlctx.prefix = "notify action";

		if ( method->runtime_check_operands
			(&nlctx, method_uri, uri, message, from, sieve_result_pool(renv->result), 
			context) )
			return method;
		
		return NULL;
	}

	*context = NULL;	
	return method;
}

/*
 * Method logging
 */

void sieve_enotify_error
(const struct sieve_enotify_log_context *nlctx, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	
	T_BEGIN {
		if ( nlctx->prefix == NULL )
			sieve_verror(nlctx->ehandler, nlctx->location, fmt, args);
		else
			sieve_error(nlctx->ehandler, nlctx->location, "%s: %s", nlctx->prefix, 
				t_strdup_vprintf(fmt, args));
	} T_END;
	
	va_end(args);
}

void sieve_enotify_warning
(const struct sieve_enotify_log_context *nlctx, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	
	T_BEGIN { 
		if ( nlctx->prefix == NULL )
			sieve_vwarning(nlctx->ehandler, nlctx->location, fmt, args);
		else			
			sieve_warning(nlctx->ehandler, nlctx->location, "%s: %s", nlctx->prefix, 
				t_strdup_vprintf(fmt, args));
	} T_END;
	
	va_end(args);
}

void sieve_enotify_log
(const struct sieve_enotify_log_context *nlctx, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	
	T_BEGIN { 
		if ( nlctx->prefix == NULL )
			sieve_vinfo(nlctx->ehandler, nlctx->location, fmt, args);
		else
			sieve_info(nlctx->ehandler, nlctx->location, "%s: %s", nlctx->prefix, 
				t_strdup_vprintf(fmt, args));	
	} T_END;
	
	va_end(args);
}


