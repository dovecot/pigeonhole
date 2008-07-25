#include "lib.h"
#include "str.h"

#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"
#include "sieve-dump.h"

#include "sieve-ext-variables.h"

#include "ext-imapflags-common.h"

/* Tagged arguments */

extern const struct sieve_argument tag_flags;
extern const struct sieve_argument tag_flags_implicit;

/*
 * Forward declarations
 */

static bool flag_is_valid(const char *flag);

/* Common functions */

bool ext_imapflags_command_validate
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
		sieve_command_validate_error(validator, cmd, 
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
			sieve_command_validate_error(validator, cmd, 
				"if a second argument is specified for the %s %s, the first "
				"must be a string (variable name), but %s was found",
				cmd->command->identifier, sieve_command_type_name(cmd->command), 
				sieve_ast_argument_name(arg));
			return FALSE; 
		}
		
		/* Then, check whether the second argument is permitted */
		
		if ( !sieve_ext_variables_is_active(validator) )	{
			sieve_command_validate_error(validator, cmd, 
				"the %s %s only allows for the specification of a "
				"variable name when the variables extension is active",
				cmd->command->identifier, sieve_command_type_name(cmd->command));
			return FALSE;
		}		
		
		if ( !sieve_variable_argument_activate(validator, cmd, arg, TRUE) )
			return FALSE;
		
		if ( sieve_ast_argument_type(arg2) != SAAT_STRING && 
			sieve_ast_argument_type(arg2) != SAAT_STRING_LIST ) 
		{
			sieve_command_validate_error(validator, cmd, 
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
		struct ext_imapflags_iter fiter;
		const char *flag;
		
		/* Warn the user about validity of verifiable flags */
		ext_imapflags_iter_init(&fiter, sieve_ast_argument_str(arg));

		while ( (flag=ext_imapflags_iter_get_flag(&fiter)) != NULL ) {
			if ( !flag_is_valid(flag) ) {
				sieve_command_validate_warning(validator, cmd,
                	"IMAP flag '%s' specified for the %s command is invalid "
					"and will be ignored (only first invalid is reported)",					
					flag, cmd->command->identifier);
				break;
			}
		}
	}

	return TRUE;
}

bool ext_imapflags_command_operands_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct sieve_operand *operand;
	
	sieve_code_mark(denv);
	operand = sieve_operand_read(denv->sbin, address);

	if ( sieve_operand_is_variable(operand) ) {	
		return 
			sieve_opr_string_dump_data(denv, operand, address) &&
			sieve_opr_stringlist_dump(denv, address);
	}
	
	return 
			sieve_opr_stringlist_dump_data(denv, operand, address);
}

bool ext_imapflags_command_operation_dump
(const struct sieve_operation *op,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "%s", op->mnemonic);
	sieve_code_descend(denv);

	return ext_imapflags_command_operands_dump(denv, address); 
}

int ext_imapflags_command_operands_read
(const struct sieve_runtime_env *renv, sieve_size_t *address,
	struct sieve_coded_stringlist **flag_list, 
	struct sieve_variable_storage **storage, unsigned int *var_index)
{
	sieve_size_t op_address = *address;
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);

	if ( operand == NULL ) {
		sieve_runtime_trace_error(renv, "invalid operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
		
	if ( sieve_operand_is_variable(operand) ) {		
		/* Read the variable operand */
		if ( !sieve_variable_operand_read_data
			(renv, operand, address, storage, var_index) ) {
			sieve_runtime_trace_error(renv, "invalid variable operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
		
		/* Read flag list */
		if ( (*flag_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
			sieve_runtime_trace_error(renv, "invalid flag-list operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	} else if ( sieve_operand_is_stringlist(operand) ) {	
		*storage = NULL;
		*var_index = 0;
		
		/* Read flag list */
		if ( (*flag_list=sieve_opr_stringlist_read_data
			(renv, operand, op_address, address)) == NULL ) {
			sieve_runtime_trace_error(renv, "invalid flag-list operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	} else {
		sieve_runtime_trace_error(renv, "unexpected operand '%s'", 
			operand->name);
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	return SIEVE_EXEC_OK;
}

/* Flags tag registration */

void ext_imapflags_attach_flags_tag
(struct sieve_validator *valdtr, const char *command)
{
	/* Register :flags tag with the command and we don't care whether it is 
	 * registered or even whether it will be registered at all. The validator 
	 * handles either situation gracefully 
	 */
	 
	/* Tag specified by user */
	sieve_validator_register_external_tag
		(valdtr, &tag_flags, command, -1);
}

/* Context access */

struct ext_imapflags_result_context {
    string_t *internal_flags;
};

static inline struct ext_imapflags_result_context *
	_get_result_context(struct sieve_result *result)
{
	struct ext_imapflags_result_context *rctx =
		(struct ext_imapflags_result_context *) 
		sieve_result_extension_get_context(result, &imapflags_extension);

	if ( rctx == NULL ) {
		pool_t pool = sieve_result_pool(result);

		rctx =p_new(pool, struct ext_imapflags_result_context, 1);
		rctx->internal_flags = str_new(pool, 32);

		sieve_result_extension_set_context
			(result, &imapflags_extension, rctx);
	}

	return rctx;
}

static string_t *_get_flags_string
	(struct sieve_result *result)
{
	struct ext_imapflags_result_context *ctx = 
		_get_result_context(result);
		
	return ctx->internal_flags;
}

/* Initialization */

void ext_imapflags_runtime_init(const struct sieve_runtime_env *renv)
{	
	sieve_result_add_implicit_side_effect
		(renv->result, &act_store, &flags_side_effect, NULL);
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
		/* Custom keyword:
		 *
		 * The validity of the keyword cannot be validated until the 
		 * target mailbox for the message is known. Meaning that the 
		 * verfication of keyword can only be performed when the
		 * action side effect is about to be executed.
		 */					
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
(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
	unsigned int var_index, string_t *flags)
{
	string_t *cur_flags;
	
	if ( storage != NULL ) 
		sieve_variable_get_modifiable(storage, var_index, &cur_flags);
	else
		cur_flags = _get_flags_string(renv->result);

	flags_list_set_flags(cur_flags, flags);		
}

void ext_imapflags_add_flags
(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
	unsigned int var_index, string_t *flags)
{
	string_t *cur_flags;
	
	if ( storage != NULL ) 
		sieve_variable_get_modifiable(storage, var_index, &cur_flags);
	else
		cur_flags = _get_flags_string(renv->result);
	
	flags_list_add_flags(cur_flags, flags);		
}

void ext_imapflags_remove_flags
(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
	unsigned int var_index, string_t *flags)
{
	string_t *cur_flags;
	
	if ( storage != NULL ) 
		sieve_variable_get_modifiable(storage, var_index, &cur_flags);
	else
		cur_flags = _get_flags_string(renv->result);
	
	flags_list_remove_flags(cur_flags, flags);		
}

const char *ext_imapflags_get_flags_string
(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage, 
	unsigned int var_index)
{
	string_t *cur_flags;
	
	if ( storage != NULL ) 
		sieve_variable_get_modifiable(storage, var_index, &cur_flags);
	else
		cur_flags = _get_flags_string(renv->result);

	return str_c(cur_flags);
}

void ext_imapflags_get_flags_init
(struct ext_imapflags_iter *iter, const struct sieve_runtime_env *renv,
	struct sieve_variable_storage *storage, unsigned int var_index)
{
	string_t *cur_flags;
	
	if ( storage != NULL ) 
		sieve_variable_get_modifiable(storage, var_index, &cur_flags);
	else
		cur_flags = _get_flags_string(renv->result);
	
	ext_imapflags_iter_init(iter, cur_flags);		
}

void ext_imapflags_get_implicit_flags_init
(struct ext_imapflags_iter *iter, struct sieve_result *result)
{
	string_t *cur_flags = _get_flags_string(result);
	
	ext_imapflags_iter_init(iter, cur_flags);		
}


	
	

