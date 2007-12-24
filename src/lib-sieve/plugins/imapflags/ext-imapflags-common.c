#include "lib.h"

#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

#include "ext-imapflags-common.h"

bool ext_imapflags_command_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct sieve_ast_argument *arg2;
	
	/* Check arguments */
	
	if ( arg == NULL ) {
		sieve_command_validate_error(validator, cmd, 
			"the %s command expects at least one argument, but none was found", 
			cmd->command->identifier);
		return FALSE;
	}
	
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && 
		sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) 
	{
		sieve_command_validate_error(validator, cmd, 
			"the %s command expects either a string (variable name) or "
			"a string-list (list of flags) as first argument, but %s was found", 
			cmd->command->identifier, sieve_ast_argument_name(arg));
		return FALSE; 
	}

	arg2 = sieve_ast_argument_next(arg);
	if ( arg2 != NULL ) {		
		sieve_validator_argument_activate(validator, cmd, arg, TRUE);

		/* First, check syntax sanity */
				
		if ( sieve_ast_argument_type(arg) != SAAT_STRING ) 
		{
			sieve_command_validate_error(validator, cmd, 
				"if a second argument is specified for the %s command, the first "
				"must be a string (variable name), but %s was found",
				cmd->command->identifier, sieve_ast_argument_name(arg));
			return FALSE; 
		}		
		
		if ( sieve_ast_argument_type(arg2) != SAAT_STRING && 
			sieve_ast_argument_type(arg2) != SAAT_STRING_LIST ) 
		{
			sieve_command_validate_error(validator, cmd, 
				"the %s command expects a string list (list of flags) as "
				"second argument when two arguments are specified, "
				"but %s was found",
				cmd->command->identifier, sieve_ast_argument_name(arg2));
			return FALSE; 
		}
		
		/* Then, check whether the second argument is permitted */
		
		/* IF !VARIABLE EXTENSION LOADED */
		{
			sieve_command_validate_error(validator, cmd, 
				"the %s command only allows for the specification of a "
				"variable name when the variables extension is active",
				cmd->command->identifier);
			return FALSE;
		}
	} else
		arg2 = arg;

	sieve_validator_argument_activate(validator, cmd, arg2, FALSE);	

	return TRUE;
}

bool ext_imapflags_command_opcode_dump
(const struct sieve_opcode *opcode,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "%s", opcode->mnemonic);
	sieve_code_descend(denv);

	return 
		sieve_opr_stringlist_dump(denv, address);
}

/* Context access */

struct ext_imapflags_message_context {
    string_t *internal_flags;
};

static inline struct ext_imapflags_message_context *
	_get_message_context(struct sieve_message_context *msgctx)
{
	struct ext_imapflags_message_context *mctx =
		(struct ext_imapflags_message_context *) 
		sieve_message_context_extension_get(msgctx, ext_imapflags_my_id);

	if ( mctx == NULL ) {
		pool_t pool = sieve_message_context_pool(msgctx);

		mctx =p_new(pool, struct ext_imapflags_message_context, 1);
		mctx->internal_flags = str_new(pool, 32);

		sieve_message_context_extension_set	
			(msgctx, ext_imapflags_my_id, mctx);
	}

	return mctx;
}

static string_t *_get_flags_string
	(struct sieve_message_context *msgctx)
{
	struct ext_imapflags_message_context *ctx = 
		_get_message_context(msgctx);
		
	return ctx->internal_flags;
}

/* Flag operations */

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
		/* Custom keyword: currently nothing to validate */					
	}

	return TRUE;  
}

void ext_imapflags_iter_init
	(struct ext_imapflags_iter *iter, string_t *flags_list) 
{
	iter->flags_list = flags_list;
	iter->offset = 0;
	iter->last = 0;
}

const char *ext_imapflags_iter_get_flag
	(struct ext_imapflags_iter *iter) 
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

static void ext_imapflags_iter_delete_last
	(struct ext_imapflags_iter *iter) 
{
	iter->offset++;
	if ( iter->offset > str_len(iter->flags_list) )
		iter->offset = str_len(iter->flags_list);
	if ( iter->offset == str_len(iter->flags_list) )
		iter->last--;

	str_delete(iter->flags_list, iter->last, iter->offset - iter->last);	
	
	iter->offset = iter->last;
}

static bool flags_list_flag_exists(string_t *flags_list, const char *flag)
{
	const char *flg;
	struct ext_imapflags_iter flit;
		
	ext_imapflags_iter_init(&flit, flags_list);
	
	while ( (flg=ext_imapflags_iter_get_flag(&flit)) != NULL ) {
		if ( strcasecmp(flg, flag) == 0 ) 
			return TRUE; 	
	}
	
	return FALSE;
}

static void flags_list_flag_delete(string_t *flags_list, const char *flag)
{
	const char *flg;
	struct ext_imapflags_iter flit;
		
	ext_imapflags_iter_init(&flit, flags_list);
	
	while ( (flg=ext_imapflags_iter_get_flag(&flit)) != NULL ) {
		if ( strcasecmp(flg, flag) == 0 ) {
			ext_imapflags_iter_delete_last(&flit);
		} 	
	}
}
 			
static void flags_list_add_flags
	(string_t *flags_list, string_t *flags)
{	
	const char *flg;
	struct ext_imapflags_iter flit;
		
	ext_imapflags_iter_init(&flit, flags);
	
	while ( (flg=ext_imapflags_iter_get_flag(&flit)) != NULL ) {
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
	struct ext_imapflags_iter flit;
		
	ext_imapflags_iter_init(&flit, flags);
	
	while ( (flg=ext_imapflags_iter_get_flag(&flit)) != NULL ) {
		flags_list_flag_delete(flags_list, flg); 	
	}
}

static void flags_list_set_flags
	(string_t *flags_list, string_t *flags)
{
	str_truncate(flags_list, 0);
	flags_list_add_flags(flags_list, flags);
}

/* Flag registration */

void ext_imapflags_set_flags
	(const struct sieve_runtime_env *renv, string_t *flags)
{
	string_t *cur_flags = _get_flags_string(renv->msgctx);

	flags_list_set_flags(cur_flags, flags);		
}

void ext_imapflags_add_flags
	(const struct sieve_runtime_env *renv, string_t *flags)
{
	string_t *cur_flags = _get_flags_string(renv->msgctx);
	
	flags_list_add_flags(cur_flags, flags);		
}

void ext_imapflags_remove_flags
	(const struct sieve_runtime_env *renv, string_t *flags)
{
	string_t *cur_flags = _get_flags_string(renv->msgctx);
	
	flags_list_remove_flags(cur_flags, flags);		
}

const char *ext_imapflags_get_flags_string
	(const struct sieve_runtime_env *renv)
{
	return str_c(_get_flags_string(renv->msgctx));
}

void ext_imapflags_get_flags_init
	(struct ext_imapflags_iter *iter, const struct sieve_runtime_env *renv)
{
	string_t *cur_flags = _get_flags_string(renv->msgctx);
	
	ext_imapflags_iter_init(iter, cur_flags);		
}

	
	

