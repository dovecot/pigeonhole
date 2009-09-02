/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"
#include "sieve-dump.h"

#include "sieve-ext-variables.h"

#include "ext-imap4flags-common.h"

/*
 * Forward declarations
 */

static bool flag_is_valid(const char *flag);

/* 
 * Tagged arguments 
 */

extern const struct sieve_argument tag_flags;
extern const struct sieve_argument tag_flags_implicit;

/* 
 * Common command functions 
 */

bool ext_imap4flags_command_validate
(struct sieve_validator *validator, struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct sieve_ast_argument *arg2;
	
	/* Check arguments */
	
	if ( arg == NULL ) {
		sieve_command_validate_error(validator, cmd, 
			"the %s %s expects at least one argument, but none was found", 
			cmd->command->identifier, sieve_command_type_name(cmd->command));
		return FALSE;
	}
	
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && 
		sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) 
	{
		sieve_argument_validate_error(validator, arg, 
			"the %s %s expects either a string (variable name) or "
			"a string-list (list of flags) as first argument, but %s was found", 
			cmd->command->identifier, sieve_command_type_name(cmd->command),
			sieve_ast_argument_name(arg));
		return FALSE; 
	}

	arg2 = sieve_ast_argument_next(arg);
	if ( arg2 != NULL ) {		
		/* First, check syntax sanity */
				
		if ( sieve_ast_argument_type(arg) != SAAT_STRING ) 
		{
			if ( cmd->command == &tst_hasflag ) {
				if ( sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) {
					sieve_argument_validate_error(validator, arg, 
						"if a second argument is specified for the hasflag, the first "
						"must be a string-list (variable-list), but %s was found",
						sieve_ast_argument_name(arg));
					return FALSE;
				}
			} else {
				sieve_argument_validate_error(validator, arg, 
					"if a second argument is specified for the %s %s, the first "
					"must be a string (variable name), but %s was found",
					cmd->command->identifier, sieve_command_type_name(cmd->command), 
					sieve_ast_argument_name(arg));
				return FALSE; 
			}
		}
		
		/* Then, check whether the second argument is permitted */
		
		if ( !sieve_ext_variables_is_active(validator) )	{
			sieve_argument_validate_error(validator,arg, 
				"the %s %s only allows for the specification of a "
				"variable name when the variables extension is active",
				cmd->command->identifier, sieve_command_type_name(cmd->command));
			return FALSE;
		}		
		
		if ( !sieve_variable_argument_activate(validator, cmd, arg, 
			cmd->command != &tst_hasflag ) )
			return FALSE;
		
		if ( sieve_ast_argument_type(arg2) != SAAT_STRING && 
			sieve_ast_argument_type(arg2) != SAAT_STRING_LIST ) 
		{
			sieve_argument_validate_error(validator, arg2, 
				"the %s %s expects a string list (list of flags) as "
				"second argument when two arguments are specified, "
				"but %s was found",
				cmd->command->identifier, sieve_command_type_name(cmd->command),
				sieve_ast_argument_name(arg2));
			return FALSE; 
		}
	} else
		arg2 = arg;

	if ( !sieve_validator_argument_activate(validator, cmd, arg2, FALSE) )
		return FALSE;

	if ( cmd->command != &tst_hasflag && sieve_argument_is_string_literal(arg2) ) {
		struct ext_imap4flags_iter fiter;
		const char *flag;
		
		/* Warn the user about validity of verifiable flags */
		ext_imap4flags_iter_init(&fiter, sieve_ast_argument_str(arg));

		while ( (flag=ext_imap4flags_iter_get_flag(&fiter)) != NULL ) {
			if ( !flag_is_valid(flag) ) {
				sieve_argument_validate_warning(validator, arg,
                	"IMAP flag '%s' specified for the %s command is invalid "
					"and will be ignored (only first invalid is reported)",					
					str_sanitize(flag, 64), cmd->command->identifier);
				break;
			}
		}
	}

	return TRUE;
}

/* 
 * Flags tag registration 
 */

void ext_imap4flags_attach_flags_tag
(struct sieve_validator *valdtr, const char *command)
{
	/* Register :flags tag with the command and we don't care whether it is 
	 * registered or even whether it will be registered at all. The validator 
	 * handles either situation gracefully 
	 */
	 
	/* Tag specified by user */
	sieve_validator_register_external_tag
		(valdtr, &tag_flags, command, SIEVE_OPT_SIDE_EFFECT);

    /* Implicit tag if none is specified */
	sieve_validator_register_persistent_tag
		(valdtr, &tag_flags_implicit, command);
}

/* 
 * Result context 
 */

struct ext_imap4flags_result_context {
    string_t *internal_flags;
};

static void _get_initial_flags
(struct sieve_result *result, string_t *flags)
{
	const struct sieve_message_data *msgdata = 
		sieve_result_get_message_data(result);
	enum mail_flags mail_flags;
	const char *const *mail_keywords;

	mail_flags = mail_get_flags(msgdata->mail);
	mail_keywords = mail_get_keywords(msgdata->mail);	

	if ( (mail_flags & MAIL_FLAGGED) > 0 )
		str_printfa(flags, " \\flagged");

	if ( (mail_flags & MAIL_ANSWERED) > 0 )
		str_printfa(flags, " \\answered");

	if ( (mail_flags & MAIL_DELETED) > 0 )
		str_printfa(flags, " \\deleted");

	if ( (mail_flags & MAIL_SEEN) > 0 )
		str_printfa(flags, " \\seen");

	if ( (mail_flags & MAIL_DRAFT) > 0 )
		str_printfa(flags, " \\draft");

	while ( *mail_keywords != NULL ) {
		str_printfa(flags, " %s", *mail_keywords);
		mail_keywords++;
	}	
}

static inline struct ext_imap4flags_result_context *_get_result_context
(struct sieve_result *result)
{
	struct ext_imap4flags_result_context *rctx =
		(struct ext_imap4flags_result_context *) 
		sieve_result_extension_get_context(result, &imap4flags_extension);

	if ( rctx == NULL ) {
		pool_t pool = sieve_result_pool(result);

		rctx =p_new(pool, struct ext_imap4flags_result_context, 1);
		rctx->internal_flags = str_new(pool, 32);
		_get_initial_flags(result, rctx->internal_flags);

		sieve_result_extension_set_context
			(result, &imap4flags_extension, rctx);
	}

	return rctx;
}

static string_t *_get_flags_string
(struct sieve_result *result)
{
	struct ext_imap4flags_result_context *ctx = 
		_get_result_context(result);
		
	return ctx->internal_flags;
}

/* 
 * Runtime initialization 
 */

static void ext_imap4flags_runtime_init
(const struct sieve_runtime_env *renv, void *context ATTR_UNUSED)
{	
	sieve_result_add_implicit_side_effect
		(renv->result, NULL, TRUE, &flags_side_effect, NULL);
}

const struct sieve_interpreter_extension imap4flags_interpreter_extension = {
	&imap4flags_extension,
	ext_imap4flags_runtime_init,
	NULL,
};

/* 
 * Flag operations 
 */

/* FIXME: This currently accepts a potentially unlimited number of 
 * flags, making the internal or variable flag list indefinitely long
 */
static bool flag_is_valid(const char *flag)
{	
	if (*flag == '\\') {
		/* System flag */
		const char *atom = t_str_ucase(flag); 
        
		if (
			(strcmp(atom, "\\ANSWERED") != 0) &&
			(strcmp(atom, "\\FLAGGED") != 0) &&
			(strcmp(atom, "\\DELETED") != 0) &&
			(strcmp(atom, "\\SEEN") != 0) &&
			(strcmp(atom, "\\DRAFT") != 0) )  
		{           
			return FALSE;
		}
	} else {
		/* Custom keyword:
		 *
		 * The validity of the keyword cannot be validated until the 
		 * target mailbox for the message is known. Meaning that the 
		 * verfication of keyword can only be performed when the
		 * action side effect is about to be executed.
		 *
		 * FIXME: technically this is nonsense, since we can simply parse
		 * using the flag-keyword grammar provided by imap.
		 */					
	}

	return TRUE;  
}

void ext_imap4flags_iter_init
(struct ext_imap4flags_iter *iter, string_t *flags_list) 
{
	iter->flags_list = flags_list;
	iter->offset = 0;
	iter->last = 0;
}

const char *ext_imap4flags_iter_get_flag
(struct ext_imap4flags_iter *iter) 
{
	unsigned int len = str_len(iter->flags_list);
	const unsigned char *fp;
	const unsigned char *fbegin;
	const unsigned char *fstart;
	const unsigned char *fend;
	
	if ( iter->offset >= len ) return NULL;
	
	fbegin = str_data(iter->flags_list);
	fp = fbegin + iter->offset;
	fstart = fp;
	fend = fbegin + len;
	for (;;) {
		if ( fp >= fend || *fp == ' ' ) { 
			if ( fp > fstart ) {
				const char *flag = t_strdup_until(fstart, fp);
				
				iter->last = fstart - fbegin;
				iter->offset = fp - fbegin;
				return flag;
			} 	
			
			fstart = fp+1;
		}
		
		if ( fp >= fend ) break;
				
		fp++;
	}
	
	iter->last = fstart - fbegin;
	iter->offset = fp - fbegin;
	return NULL;
}

static void ext_imap4flags_iter_delete_last
(struct ext_imap4flags_iter *iter) 
{
	iter->offset++;
	if ( iter->offset > str_len(iter->flags_list) )
		iter->offset = str_len(iter->flags_list);
	if ( iter->offset == str_len(iter->flags_list) )
		iter->last--;

	str_delete(iter->flags_list, iter->last, iter->offset - iter->last);	
	
	iter->offset = iter->last;
}

static bool flags_list_flag_exists
(string_t *flags_list, const char *flag)
{
	const char *flg;
	struct ext_imap4flags_iter flit;
		
	ext_imap4flags_iter_init(&flit, flags_list);
	
	while ( (flg=ext_imap4flags_iter_get_flag(&flit)) != NULL ) {
		if ( strcasecmp(flg, flag) == 0 ) 
			return TRUE; 	
	}
	
	return FALSE;
}

static void flags_list_flag_delete
(string_t *flags_list, const char *flag)
{
	const char *flg;
	struct ext_imap4flags_iter flit;
		
	ext_imap4flags_iter_init(&flit, flags_list);
	
	while ( (flg=ext_imap4flags_iter_get_flag(&flit)) != NULL ) {
		if ( strcasecmp(flg, flag) == 0 ) {
			ext_imap4flags_iter_delete_last(&flit);
		} 	
	}
}
 			
static void flags_list_add_flags
(string_t *flags_list, string_t *flags)
{	
	const char *flg;
	struct ext_imap4flags_iter flit;
		
	ext_imap4flags_iter_init(&flit, flags);
	
	while ( (flg=ext_imap4flags_iter_get_flag(&flit)) != NULL ) {
		if ( flag_is_valid(flg) && !flags_list_flag_exists(flags_list, flg) ) {
			if ( str_len(flags_list) != 0 ) 
				str_append_c(flags_list, ' '); 
			str_append(flags_list, flg);
		} 	
	}
}

static void flags_list_remove_flags
(string_t *flags_list, string_t *flags)
{	
	const char *flg;
	struct ext_imap4flags_iter flit;
		
	ext_imap4flags_iter_init(&flit, flags);
	
	while ( (flg=ext_imap4flags_iter_get_flag(&flit)) != NULL ) {
		flags_list_flag_delete(flags_list, flg); 	
	}
}

static void flags_list_set_flags
(string_t *flags_list, string_t *flags)
{
	str_truncate(flags_list, 0);
	flags_list_add_flags(flags_list, flags);
}

/* 
 * Flag registration 
 */

int ext_imap4flags_set_flags
(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
	unsigned int var_index, string_t *flags)
{
	string_t *cur_flags;
	
	if ( storage != NULL ) {
		if ( !sieve_variable_get_modifiable(storage, var_index, &cur_flags) )
			return SIEVE_EXEC_BIN_CORRUPT;
	} else
		cur_flags = _get_flags_string(renv->result);

	if ( cur_flags != NULL )
		flags_list_set_flags(cur_flags, flags);		

	return SIEVE_EXEC_OK;
}

int ext_imap4flags_add_flags
(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
	unsigned int var_index, string_t *flags)
{
	string_t *cur_flags;
	
	if ( storage != NULL ) {
		if ( !sieve_variable_get_modifiable(storage, var_index, &cur_flags) )
			return SIEVE_EXEC_BIN_CORRUPT;
	} else
		cur_flags = _get_flags_string(renv->result);
	
	if ( cur_flags != NULL )
		flags_list_add_flags(cur_flags, flags);
	
	return SIEVE_EXEC_OK;	
}

int ext_imap4flags_remove_flags
(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
	unsigned int var_index, string_t *flags)
{
	string_t *cur_flags;
	
	if ( storage != NULL ) {
		if ( !sieve_variable_get_modifiable(storage, var_index, &cur_flags) )
			return SIEVE_EXEC_BIN_CORRUPT;
	} else
		cur_flags = _get_flags_string(renv->result);
	
	if ( cur_flags != NULL )
		flags_list_remove_flags(cur_flags, flags);		

	return SIEVE_EXEC_OK;
}

int ext_imap4flags_get_flags_string
(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage, 
	unsigned int var_index, const char **flags)
{
	string_t *cur_flags;
	
	if ( storage != NULL ) {
		if ( !sieve_variable_get_modifiable(storage, var_index, &cur_flags) )
			return SIEVE_EXEC_BIN_CORRUPT;
	} else
		cur_flags = _get_flags_string(renv->result);
	
	if ( cur_flags == NULL )
		*flags = "";
	else 
		*flags = str_c(cur_flags);

	return SIEVE_EXEC_OK;
}

void ext_imap4flags_get_flags_init
(struct ext_imap4flags_iter *iter, const struct sieve_runtime_env *renv,
	string_t *flags_list)
{
	string_t *cur_flags;
	
	if ( flags_list != NULL ) {
		cur_flags = t_str_new(256);
		
		flags_list_set_flags(cur_flags, flags_list);
	}
	else
		cur_flags = _get_flags_string(renv->result);
	
	ext_imap4flags_iter_init(iter, cur_flags);		
}

void ext_imap4flags_get_implicit_flags_init
(struct ext_imap4flags_iter *iter, struct sieve_result *result)
{
	string_t *cur_flags = _get_flags_string(result);
	
	ext_imap4flags_iter_init(iter, cur_flags);		
}


	
	

