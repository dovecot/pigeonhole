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
#include "sieve-generator.h"
#include "sieve-dump.h"

#include "ext-variables-common.h"
#include "ext-variables-limits.h"
#include "ext-variables-name.h"
#include "ext-variables-arguments.h"

/*
 * Common error messages
 */

static inline void _ext_variables_scope_size_error
(struct sieve_validator *valdtr, struct sieve_command_context *cmd,
	const char *variable)
{
	sieve_command_validate_error(valdtr, cmd, 
		"(implicit) declaration of new variable '%s' exceeds the limit "
		"(max variables: %u)", variable, 
		SIEVE_VARIABLES_MAX_SCOPE_SIZE);
}

static inline void _ext_variables_match_index_error
(struct sieve_validator *valdtr, struct sieve_command_context *cmd,
	unsigned int variable_index)
{
	sieve_command_validate_error(valdtr, cmd, 
		"match value index %u out of range (max: %u)", variable_index, 
		SIEVE_VARIABLES_MAX_MATCH_INDEX);
}

/* 
 * Variable argument 
 */

static bool arg_variable_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command_context *context);

const struct sieve_argument variable_argument = { 
	"@variable", 
	NULL, NULL, NULL, NULL,
	arg_variable_generate 
};

static struct sieve_ast_argument *ext_variables_variable_argument_create
(struct sieve_validator *validator, struct sieve_ast *ast, 
	unsigned int source_line, const char *variable)
{
	struct sieve_variable *var;
	struct sieve_ast_argument *arg;
	
	var = ext_variables_validator_get_variable(validator, variable, TRUE);

	if ( var == NULL ) 
		return NULL;
	
	arg = sieve_ast_argument_create(ast, source_line);
	arg->type = SAAT_STRING;
	arg->argument = &variable_argument;
	arg->context = (void *) var;
	
	return arg;
}

static bool _sieve_variable_argument_activate
(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	struct sieve_ast_argument *arg, bool assignment)
{
	bool result = FALSE;
	struct sieve_variable *var;
	string_t *variable;
	const char *varstr, *varend;
	ARRAY_TYPE(ext_variable_name) vname;	
	int nelements = 0;

	T_BEGIN {
		t_array_init(&vname, 2);			
	
		variable = sieve_ast_argument_str(arg);
		varstr = str_c(variable);
		varend = PTR_OFFSET(varstr, str_len(variable));
		nelements = ext_variable_name_parse(&vname, &varstr, varend);

		/* Check whether name parsing succeeded */	
		if ( nelements < 0 || varstr != varend ) {
			/* Parse failed */
			sieve_command_validate_error(validator, cmd, 
				"invalid variable name '%s'", str_sanitize(str_c(variable),80));
		} else if ( nelements == 1 ) {
			/* Normal (match) variable */

			const struct ext_variable_name *cur_element = 
				array_idx(&vname, 0);

			if ( cur_element->num_variable < 0 ) {
				/* Variable */
				var = ext_variables_validator_get_variable
					(validator, str_c(cur_element->identifier), TRUE);

				if ( var == NULL ) {
					_ext_variables_scope_size_error
						(validator, cmd, str_c(cur_element->identifier));
				} else {
					arg->argument = &variable_argument;
					arg->context = (void *) var;
				
					result = TRUE;
				}
			} else {
				/* Match value */
				if ( !assignment ) {
					if ( cur_element->num_variable > SIEVE_VARIABLES_MAX_MATCH_INDEX ) {
						_ext_variables_match_index_error
							(validator, cmd, cur_element->num_variable);
					} else {
						arg->argument = &match_value_argument;
						arg->context = (void *) cur_element->num_variable;
										
						result = TRUE;
					}
				} else {		
					sieve_command_validate_error(validator, cmd, 
						"cannot assign to match variable");
				}
			}
		} else {
			/* Namespace variable */

			const struct ext_variable_name *cur_element = 
				array_idx(&vname, 0);

			/* FIXME: Variable namespaces unsupported. */
	
			/* References to namespaces without a prior require statement for 
			 * the relevant extension MUST cause an error.
			 */

			sieve_command_validate_error(validator, cmd, 
				"cannot %s to variable in unknown namespace '%s'", 
				assignment ? "assign" : "refer", str_c(cur_element->identifier));
		}
	} T_END;

	return result;
}

bool sieve_variable_argument_activate
(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	struct sieve_ast_argument *arg, bool assignment)
{
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		/* Single string */
		return _sieve_variable_argument_activate(validator, cmd, arg, assignment);
		
	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem;
		
		i_assert ( !assignment );
		
		stritem = sieve_ast_strlist_first(arg);
		while ( stritem != NULL ) {
			if ( !_sieve_variable_argument_activate
				(validator, cmd, stritem, assignment) )
				return FALSE;
			
			stritem = sieve_ast_strlist_next(stritem);
		}
		
		arg->argument = &string_list_argument;
		
		return TRUE;
	} 
	
	return FALSE;
}

static bool arg_variable_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	struct sieve_variable *var = (struct sieve_variable *) arg->context;
	
	ext_variables_opr_variable_emit(cgenv->sbin, var);

	return TRUE;
}

/* 
 * Match value argument 
 */

static bool arg_match_value_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context ATTR_UNUSED);

const struct sieve_argument match_value_argument = { 
	"@match_value", 
	NULL, NULL, NULL, NULL,
	arg_match_value_generate 
};

static struct sieve_ast_argument *ext_variables_match_value_argument_create
(struct sieve_validator *validator ATTR_UNUSED, struct sieve_ast *ast, 
	unsigned int source_line,	unsigned int index)
{
	struct sieve_ast_argument *arg;
	
	arg = sieve_ast_argument_create(ast, source_line);
	arg->type = SAAT_STRING;
	arg->argument = &match_value_argument;
	arg->context = (void *) index;
	
	return arg;
}

static bool arg_match_value_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	unsigned int index = (unsigned int) arg->context;
	
	ext_variables_opr_match_value_emit(cgenv->sbin, index);

	return TRUE;
}

/* 
 * Variable string argument 
 */

static bool arg_variable_string_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *context);
static bool arg_variable_string_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context);

const struct sieve_argument variable_string_argument = { 
	"@variable-string", 
	NULL, NULL,
	arg_variable_string_validate, 
	NULL, 
	arg_variable_string_generate,
};

struct _variable_string_data {
	struct sieve_ast_arg_list *str_parts;
};

inline static struct sieve_ast_argument *_add_string_element
(struct sieve_ast_arg_list *list, struct sieve_ast_argument *arg)
{
	struct sieve_ast_argument *strarg = 
		sieve_ast_argument_create(arg->ast, arg->source_line);
	sieve_ast_arg_list_add(list, strarg);
	strarg->type = SAAT_STRING;

	return strarg;
}

static bool arg_variable_string_validate
(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd)
{
	enum { ST_NONE, ST_OPEN, ST_VARIABLE, ST_CLOSE } state = ST_NONE;
	pool_t pool = sieve_ast_pool((*arg)->ast);
	struct sieve_ast_arg_list *arglist = NULL;
	string_t *str = sieve_ast_argument_str(*arg);
	const char *p, *strstart, *substart = NULL;
	const char *strval = (const char *) str_data(str);
	const char *strend = strval + str_len(str);
	struct _variable_string_data *strdata;
	bool result = TRUE;

	ARRAY_TYPE(ext_variable_name) substitution;	
	int nelements = 0;
	
	T_BEGIN {
		/* Initialize substitution structure */
		t_array_init(&substitution, 2);		
	
		p = strval;
		strstart = p;
		while ( result && p < strend ) {
			switch ( state ) {

			/* Nothing found yet */
			case ST_NONE:
				if ( *p == '$' ) {
					substart = p;
					state = ST_OPEN;
				}
				p++;
				break;

			/* Got '$' */
			case ST_OPEN:
				if ( *p == '{' ) {
					state = ST_VARIABLE;
					p++;
				} else 
					state = ST_NONE;
				break;

			/* Got '${' */ 
			case ST_VARIABLE:
				nelements = ext_variable_name_parse(&substitution, &p, strend);
			
				if ( nelements < 0 )
					state = ST_NONE;
				else 
					state = ST_CLOSE;
			
				break;

			/* Finished parsing name, expecting '}' */
			case ST_CLOSE:
				if ( *p == '}' ) {				
					struct sieve_ast_argument *strarg;
				
					/* We now know that the substitution is valid */	
					
					if ( arglist == NULL ) {
						arglist = sieve_ast_arg_list_create(pool);
					}
				
					/* Add the substring that is before the substitution to the 
					 * variable-string AST.
					 *
					 * FIXME: For efficiency, if the variable is not found we should 
					 * coalesce this substring with the one after the substitution.
					 */
					if ( substart > strstart ) {
						strarg = _add_string_element(arglist, *arg);
						strarg->_value.str = str_new(pool, substart - strstart);
						str_append_n(strarg->_value.str, strstart, substart - strstart); 
					
						/* Give other substitution extensions a chance to do their work */
						if ( !sieve_validator_argument_activate_super
							(validator, cmd, strarg, FALSE) ) {
							result = FALSE;
							break;
						}
					}
				
					/* Find the variable */
					if ( nelements == 1 ) {
						const struct ext_variable_name *cur_element = 
							array_idx(&substitution, 0);
						
						if ( cur_element->num_variable == -1 ) {
							/* Add variable argument '${identifier}' */
							string_t *cur_ident = cur_element->identifier; 
						
							strarg = ext_variables_variable_argument_create
								(validator, (*arg)->ast, (*arg)->source_line, str_c(cur_ident));
							if ( strarg != NULL )
								sieve_ast_arg_list_add(arglist, strarg);
							else {
								_ext_variables_scope_size_error
									(validator, cmd, str_c(cur_element->identifier));
								result = FALSE;
								break;
							}
						} else {
							/* Add match value argument '${000}' */
							if ( cur_element->num_variable > SIEVE_VARIABLES_MAX_MATCH_INDEX ) {
								_ext_variables_match_index_error
									(validator, cmd, cur_element->num_variable);
								result = FALSE;
								break;
							}

							strarg = ext_variables_match_value_argument_create
								(validator, (*arg)->ast, (*arg)->source_line, 
								cur_element->num_variable);
							if ( strarg != NULL )
								sieve_ast_arg_list_add(arglist, strarg);
						}
					} else {
						const struct ext_variable_name *cur_element = 
							array_idx(&substitution, 0);

						/* FIXME: Namespaces are not supported. */

						/* References to namespaces without a prior require 
						 * statement for thecrelevant extension MUST cause an error.
					 	 */
						sieve_command_validate_error(validator, cmd, 
							"referring to variable in unknown namespace '%s'", 
							str_c(cur_element->identifier));
						result = FALSE;
						break;
					}
				
					strstart = p + 1;
					substart = strstart;

					p++;	
				}
		
				/* Finished, reset for the next substitution */	
				state = ST_NONE;
			}
		}
	} T_END;

	/* Bail out early if substitution is invalid */
	if ( !result ) return FALSE;
	
	/* Check whether any substitutions were found */
	if ( arglist == NULL ) {
		/* No substitutions in this string, pass it on to any other substution
		 * extension.
		 */
		return sieve_validator_argument_activate_super
			(validator, cmd, *arg, TRUE);
	}
	
	/* Add the final substring that comes after the last substitution to the 
	 * variable-string AST.
	 */
	if ( strend > strstart ) {
		struct sieve_ast_argument *strarg = _add_string_element(arglist, *arg);
		strarg->_value.str = str_new(pool, strend - strstart);
		str_append_n(strarg->_value.str, strstart, strend - strstart); 
	
		/* Give other substitution extensions a chance to do their work */	
		if ( !sieve_validator_argument_activate_super
			(validator, cmd, strarg, FALSE) )
			return FALSE;
	}	
	
	/* Assign the constructed variable-string AST-branch to the actual AST */
	strdata = p_new(pool, struct _variable_string_data, 1);
	strdata->str_parts = arglist;
	(*arg)->context = (void *) strdata;

	return TRUE;
}

#define _string_data_first(data) __AST_LIST_FIRST((data)->str_parts)
#define _string_data_count(data) __AST_LIST_COUNT((data)->str_parts)
#define _string_data_next(item) __AST_LIST_NEXT(item)

static bool arg_variable_string_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd) 
{
	struct sieve_binary *sbin = cgenv->sbin;
	struct _variable_string_data *strdata = 
		(struct _variable_string_data *) arg->context;
	struct sieve_ast_argument *strpart;
	
	if ( _string_data_count(strdata) == 1 )
		sieve_generate_argument(cgenv, _string_data_first(strdata), cmd);
	else {
		ext_variables_opr_variable_string_emit(sbin, _string_data_count(strdata));

		strpart = _string_data_first(strdata);
		while ( strpart != NULL ) {
			if ( !sieve_generate_argument(cgenv, strpart, cmd) )
				return FALSE;
			
			strpart = _string_data_next(strpart);
		}
	}
	
	return TRUE;
}
