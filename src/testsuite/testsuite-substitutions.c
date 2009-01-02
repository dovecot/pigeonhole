/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve.h"
#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-binary.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "testsuite-common.h"
#include "testsuite-substitutions.h"

/*
 * Forward declarations
 */
 
void testsuite_opr_substitution_emit
	(struct sieve_binary *sbin, const struct testsuite_substitution *tsub,
		const char *param);
			
/*
 * Testsuite substitutions
 */
 
/* FIXME: make this extendible */

enum {
	TESTSUITE_SUBSTITUTION_FILE,
	TESTSUITE_SUBSTITUTION_MAILBOX,
	TESTSUITE_SUBSTITUTION_SMTPOUT
};

static const struct testsuite_substitution testsuite_file_substitution;
static const struct testsuite_substitution testsuite_mailbox_substitution;
static const struct testsuite_substitution testsuite_smtpout_substitution;

static const struct testsuite_substitution *substitutions[] = {
	&testsuite_file_substitution,
	&testsuite_mailbox_substitution,
	&testsuite_smtpout_substitution
};

static const unsigned int substitutions_count = N_ELEMENTS(substitutions);
 
static inline const struct testsuite_substitution *testsuite_substitution_get
(unsigned int code)
{
	if ( code > substitutions_count )
		return NULL;
	
	return substitutions[code];
}

const struct testsuite_substitution *testsuite_substitution_find
(const char *identifier)
{
	unsigned int i; 
	
	for ( i = 0; i < substitutions_count; i++ ) {
		if ( strcasecmp(substitutions[i]->object.identifier, identifier) == 0 )
			return substitutions[i];
	}
	
	return NULL;
}

/*
 * Substitution argument
 */
 
static bool arg_testsuite_substitution_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command_context *context);

struct _testsuite_substitution_context {
	const struct testsuite_substitution *tsub;
	const char *param;
};

const struct sieve_argument testsuite_substitution_argument = { 
	"@testsuite-substitution", 
	NULL, NULL, NULL, NULL,
	arg_testsuite_substitution_generate 
};

struct sieve_ast_argument *testsuite_substitution_argument_create
(struct sieve_validator *validator ATTR_UNUSED, struct sieve_ast *ast, 
	unsigned int source_line, const char *substitution, const char *param)
{
	const struct testsuite_substitution *tsub;
	struct _testsuite_substitution_context *tsctx;
	struct sieve_ast_argument *arg;
	pool_t pool;
	
	tsub = testsuite_substitution_find(substitution);
	if ( tsub == NULL ) 
		return NULL;
	
	arg = sieve_ast_argument_create(ast, source_line);
	arg->type = SAAT_STRING;
	arg->argument = &testsuite_substitution_argument;

	pool = sieve_ast_pool(ast);
	tsctx = p_new(pool, struct _testsuite_substitution_context, 1);
	tsctx->tsub = tsub;
	tsctx->param = p_strdup(pool, param);
	arg->context = (void *) tsctx;
	
	return arg;
}

static bool arg_testsuite_substitution_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	struct _testsuite_substitution_context *tsctx =  
		(struct _testsuite_substitution_context *) arg->context;
	
	testsuite_opr_substitution_emit(cgenv->sbin, tsctx->tsub, tsctx->param);

	return TRUE;
}

/*
 * Substitution operand
 */

static bool opr_substitution_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address, 
		const char *field_name);
static bool opr_substitution_read_value
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str);
	
const struct sieve_opr_string_interface testsuite_substitution_interface = { 
	opr_substitution_dump,
	opr_substitution_read_value
};
		
const struct sieve_operand testsuite_substitution_operand = { 
	"test-substitution", 
	&testsuite_extension, 
	TESTSUITE_OPERAND_SUBSTITUTION,
	&string_class,
	&testsuite_substitution_interface
};

void testsuite_opr_substitution_emit
(struct sieve_binary *sbin, const struct testsuite_substitution *tsub,
	const char *param) 
{
	/* Default variable storage */
	(void) sieve_operand_emit_code(sbin, &testsuite_substitution_operand);
	(void) sieve_binary_emit_unsigned(sbin, tsub->object.code);
	(void) sieve_binary_emit_cstring(sbin, param);
}

static bool opr_substitution_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	unsigned int code = 0;
	const struct testsuite_substitution *tsub;
	string_t *param; 

	if ( !sieve_binary_read_unsigned(denv->sbin, address, &code) )
		return FALSE;
		
	tsub = testsuite_substitution_get(code);
	if ( tsub == NULL )
		return FALSE;	
			
	if ( !sieve_binary_read_string(denv->sbin, address, &param) )
		return FALSE;
	
	if ( field_name != NULL ) 
		sieve_code_dumpf(denv, "%s: TEST_SUBS %%{%s:%s}", 
			field_name, tsub->object.identifier, str_c(param));
	else
		sieve_code_dumpf(denv, "TEST_SUBS %%{%s:%s}", 
			tsub->object.identifier, str_c(param));
	return TRUE;
}

static bool opr_substitution_read_value
(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str)
{ 
	const struct testsuite_substitution *tsub;
	unsigned int code = 0;
	string_t *param;
	
	if ( !sieve_binary_read_unsigned(renv->sbin, address, &code) )
		return FALSE;
		
	tsub = testsuite_substitution_get(code);
	if ( tsub == NULL )
		return FALSE;	

	/* Parameter str can be NULL if we are requested to only skip and not 
	 * actually read the argument.
	 */	
	if ( str == NULL ) 
		return sieve_binary_read_string(renv->sbin, address, NULL);
	
	if ( !sieve_binary_read_string(renv->sbin, address, &param) )
		return FALSE;
				
	return tsub->get_value(str_c(param), str);
}

/*
 * Testsuite substitution definitions
 */
 
static bool testsuite_file_substitution_get_value
	(const char *param, string_t **result); 
static bool testsuite_mailbox_substitution_get_value
	(const char *param, string_t **result); 
static bool testsuite_smtpout_substitution_get_value
	(const char *param, string_t **result); 
 
static const struct testsuite_substitution testsuite_file_substitution = {
	SIEVE_OBJECT(
		"file", 
		&testsuite_substitution_operand, 
		TESTSUITE_SUBSTITUTION_FILE
	),
	testsuite_file_substitution_get_value
};

static const struct testsuite_substitution testsuite_mailbox_substitution = {
	SIEVE_OBJECT(
		"mailbox", 
		&testsuite_substitution_operand, 
		TESTSUITE_SUBSTITUTION_MAILBOX
	),
	testsuite_mailbox_substitution_get_value
};

static const struct testsuite_substitution testsuite_smtpout_substitution = {
	SIEVE_OBJECT(
		"smtpout",
		&testsuite_substitution_operand,
		TESTSUITE_SUBSTITUTION_SMTPOUT
	),
	testsuite_smtpout_substitution_get_value
};
 
static bool testsuite_file_substitution_get_value
	(const char *param, string_t **result)
{
	*result = t_str_new(256);

	str_printfa(*result, "[FILE: %s]", param);
	return TRUE;
}

static bool testsuite_mailbox_substitution_get_value
	(const char *param, string_t **result)
{
	*result = t_str_new(256);

	str_printfa(*result, "[MAILBOX: %s]", param);
	return TRUE;
}

static bool testsuite_smtpout_substitution_get_value
	(const char *param, string_t **result) 
{
	*result = t_str_new(256);

	str_printfa(*result, "[SMTPOUT: %s]", param);
	return TRUE;
} 
