/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"
#include "str.h"
#include "str-sanitize.h"
#include "array.h"
#include "buffer.h"
#include "mempool.h"
#include "hash.h"

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-script.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-validator.h"

#include "sieve-comparators.h"
#include "sieve-address-parts.h"

/*
 * Forward declarations
 */
 
static void sieve_validator_register_core_commands
	(struct sieve_validator *validator);
static void sieve_validator_register_core_tests
	(struct sieve_validator *validator);
	
/*
 * Types
 */
 
/* Tag registration */

struct sieve_tag_registration {
	const struct sieve_argument *tag;
	const char *identifier;	
	int id_code;
};

/* Command registration */

struct sieve_command_registration {
	const struct sieve_command *command;
	
	ARRAY_DEFINE(normal_tags, struct sieve_tag_registration *); 
	ARRAY_DEFINE(instanced_tags, struct sieve_tag_registration *); 
	ARRAY_DEFINE(persistent_tags, struct sieve_tag_registration *); 
};
 
/* Default (literal) arguments */

struct sieve_default_argument {
	const struct sieve_argument *argument;
	struct sieve_default_argument *overrides;
};

/* 
 * Validator extension
 */

struct sieve_validator_extension_reg {
	const struct sieve_validator_extension *val_ext;
	void *context;
};

/* 
 * Validator
 */

struct sieve_validator {
	pool_t pool;

	struct sieve_ast *ast;
	struct sieve_script *script;
	
	struct sieve_error_handler *ehandler;
	
	/* Registries */
	
	struct hash_table *commands;
	
	ARRAY_DEFINE(extensions, struct sieve_validator_extension_reg);
	
	/* This is currently a wee bit ugly and needs more thought */
	struct sieve_default_argument default_arguments[SAT_COUNT];

	/* Default argument processing state (FIXME: ugly) */
	struct sieve_default_argument *current_defarg;
	enum sieve_argument_type current_defarg_type;
	bool current_defarg_constant;
};

/* 
 * Error handling 
 */

void sieve_validator_warning
(struct sieve_validator *validator, unsigned int source_line, 
	const char *fmt, ...) 
{ 
	va_list args;
	
	va_start(args, fmt);
	sieve_vwarning(validator->ehandler, 
		sieve_error_script_location(validator->script, source_line),
		fmt, args);
	va_end(args);
	
}
 
void sieve_validator_error
(struct sieve_validator *validator, unsigned int source_line, 
	const char *fmt, ...) 
{
	va_list args;
	
	va_start(args, fmt);
	sieve_verror(validator->ehandler, 
		sieve_error_script_location(validator->script, source_line),
		fmt, args);
	va_end(args);
}

void sieve_validator_critical
(struct sieve_validator *validator, unsigned int source_line, 
	const char *fmt, ...) 
{
	va_list args;
	
	va_start(args, fmt);
	sieve_vcritical(validator->ehandler, 
		sieve_error_script_location(validator->script, source_line),
		fmt, args);
	va_end(args);
}

/* 
 * Validator object 
 */

struct sieve_validator *sieve_validator_create
(struct sieve_ast *ast, struct sieve_error_handler *ehandler) 
{
	unsigned int i;
	pool_t pool;
	struct sieve_validator *validator;
	
	pool = pool_alloconly_create("sieve_validator", 4096);	
	validator = p_new(pool, struct sieve_validator, 1);
	validator->pool = pool;
	
	validator->ehandler = ehandler;
	sieve_error_handler_ref(ehandler);
	
	validator->ast = ast;	
	validator->script = sieve_ast_script(ast);
	sieve_ast_ref(ast);

	/* Setup default arguments */
	validator->default_arguments[SAT_NUMBER].
		argument = &number_argument;
	validator->default_arguments[SAT_VAR_STRING].
		argument = &string_argument;
	validator->default_arguments[SAT_CONST_STRING].
		argument = &string_argument;
	validator->default_arguments[SAT_STRING_LIST].
		argument = &string_list_argument;

	/* Setup storage for extension contexts */		
	p_array_init(&validator->extensions, pool, sieve_extensions_get_count());
		
	/* Setup command registry */
	validator->commands = hash_table_create
		(default_pool, pool, 0, strcase_hash, (hash_cmp_callback_t *)strcasecmp);
	sieve_validator_register_core_commands(validator);
	sieve_validator_register_core_tests(validator);
	
	/* Pre-load core language features implemented as 'extensions' */
	for ( i = 0; i < sieve_preloaded_extensions_count; i++ ) {
		const struct sieve_extension *ext = sieve_preloaded_extensions[i];
		
		if ( ext->validator_load != NULL )
			(void)ext->validator_load(validator);
	}
	
	return validator;
}

void sieve_validator_free(struct sieve_validator **validator) 
{
	const struct sieve_validator_extension_reg *extrs;
	unsigned int ext_count, i;

	hash_table_destroy(&(*validator)->commands);
	sieve_ast_unref(&(*validator)->ast);

	sieve_error_handler_unref(&(*validator)->ehandler);

	/* Signal registered extensions that the validator is being destroyed */
	extrs = array_get(&(*validator)->extensions, &ext_count);
	for ( i = 0; i < ext_count; i++ ) {
		if ( extrs[i].val_ext != NULL && extrs[i].val_ext->free != NULL )
			extrs[i].val_ext->free(*validator, extrs[i].context);
	}

	pool_unref(&(*validator)->pool);

	*validator = NULL;
}

/*
 * Accessors
 */

pool_t sieve_validator_pool(struct sieve_validator *validator)
{
	return validator->pool;
}

struct sieve_error_handler *sieve_validator_error_handler
(struct sieve_validator *validator)
{
	return validator->ehandler;
}

struct sieve_ast *sieve_validator_ast
(struct sieve_validator *validator)
{
	return validator->ast;
}

struct sieve_script *sieve_validator_script
(struct sieve_validator *validator)
{
	return validator->script;
}

/* 
 * Command registry 
 */

/* Dummy command object to mark unknown commands in the registry */

static bool _cmd_unknown_validate
(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_command_context *cmd ATTR_UNUSED) 
{
	i_unreached();
	return FALSE;
}

static const struct sieve_command unknown_command = { 
	"", SCT_NONE, 0, 0, FALSE, FALSE , 
	NULL, NULL, _cmd_unknown_validate, NULL, NULL 
};

/* Registration of the core commands of the language */

static void sieve_validator_register_core_tests
(struct sieve_validator *validator) 
{
	unsigned int i;
	
	for ( i = 0; i < sieve_core_tests_count; i++ ) {
		sieve_validator_register_command(validator, sieve_core_tests[i]); 
	}
}

static void sieve_validator_register_core_commands
(struct sieve_validator *validator) 
{
	unsigned int i;
	
	for ( i = 0; i < sieve_core_commands_count; i++ ) {
		sieve_validator_register_command(validator, sieve_core_commands[i]); 
	}
}

/* Registry functions */

static struct sieve_command_registration *
sieve_validator_find_command_registration
(struct sieve_validator *validator, const char *command) 
{
	return (struct sieve_command_registration *) 
		hash_table_lookup(validator->commands, command);
}

static struct sieve_command_registration *_sieve_validator_register_command
(struct sieve_validator *validator, const struct sieve_command *command,
	const char *identifier) 
{
	struct sieve_command_registration *record = 
		p_new(validator->pool, struct sieve_command_registration, 1);
	record->command = command;
	hash_table_insert(validator->commands, (void *) identifier, (void *) record);
		
	return record;
}

void sieve_validator_register_command
(struct sieve_validator *validator, const struct sieve_command *command) 
{
	struct sieve_command_registration *cmd_reg =
		sieve_validator_find_command_registration(validator, command->identifier);
		
	if ( cmd_reg == NULL ) 
		cmd_reg = _sieve_validator_register_command
			(validator, command, command->identifier);
	else
		cmd_reg->command = command;
	
	if ( command->registered != NULL ) 
		command->registered(validator, cmd_reg);
}

static void sieve_validator_register_unknown_command
(struct sieve_validator *validator, const char *command) 
{
	(void)_sieve_validator_register_command(validator, &unknown_command, command);		
}

const struct sieve_command *sieve_validator_find_command
(struct sieve_validator *validator, const char *command) 
{
  struct sieve_command_registration *record = 
  	sieve_validator_find_command_registration(validator, command);
  
  return ( record == NULL ? NULL : record->command );
}

/* 
 * Per-command tagged argument registry 
 */

/* Dummy argument object to mark unknown arguments in the registry */

static bool _unknown_tag_validate
(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_ast_argument **arg ATTR_UNUSED, 
	struct sieve_command_context *tst ATTR_UNUSED)
{
	i_unreached();
	return FALSE;
}

static const struct sieve_argument _unknown_tag = { 
	"", 
	NULL, NULL, 
	_unknown_tag_validate, 
	NULL, NULL 
};

/* Registry functions */

static void _sieve_validator_register_tag
(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg, 
	const struct sieve_argument *tag, const char *identifier, int id_code) 
{
	struct sieve_tag_registration *reg;

	reg = p_new(validator->pool, struct sieve_tag_registration, 1);
	reg->tag = tag;
	reg->id_code = id_code;
	if ( identifier == NULL )
		reg->identifier = tag->identifier;
	else
		reg->identifier = p_strdup(validator->pool, identifier);
	
	if ( !array_is_created(&cmd_reg->normal_tags) )
		p_array_init(&cmd_reg->normal_tags, validator->pool, 4);

	array_append(&cmd_reg->normal_tags, &reg, 1);
}

void sieve_validator_register_persistent_tag
(struct sieve_validator *validator, const struct sieve_argument *tag, 
	const char *command)
{
	struct sieve_command_registration *cmd_reg = 
		sieve_validator_find_command_registration(validator, command);
	struct sieve_tag_registration *reg = 
		p_new(validator->pool, struct sieve_tag_registration, 1);
	
	reg->tag = tag;
	reg->id_code = -1;
	
	if ( cmd_reg == NULL ) {
		cmd_reg = _sieve_validator_register_command(validator, NULL, command);
	}	
		
	/* Add the tag to the persistent tags list if necessary */
	if ( tag->validate_persistent != NULL ) {
		if ( !array_is_created(&cmd_reg->persistent_tags) ) 
			p_array_init(&cmd_reg->persistent_tags, validator->pool, 4);
				
		array_append(&cmd_reg->persistent_tags, &reg, 1);
	}
}

void sieve_validator_register_external_tag
(struct sieve_validator *validator, const struct sieve_argument *tag, 
	const char *command, int id_code) 
{
	struct sieve_command_registration *cmd_reg = 
		sieve_validator_find_command_registration(validator, command);
		
	if ( cmd_reg == NULL ) {
		cmd_reg = _sieve_validator_register_command(validator, NULL, command);
	}
	
	_sieve_validator_register_tag
		(validator, cmd_reg, tag, NULL, id_code);
}

void sieve_validator_register_tag
(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg, 
	const struct sieve_argument *tag, int id_code) 
{
	if ( tag->is_instance_of == NULL )
		_sieve_validator_register_tag(validator, cmd_reg, tag, NULL, id_code);
	else {
		struct sieve_tag_registration *reg = 
			p_new(validator->pool, struct sieve_tag_registration, 1);
		reg->tag = tag;
		reg->id_code = id_code;

		if ( !array_is_created(&cmd_reg->instanced_tags) ) 
				p_array_init(&cmd_reg->instanced_tags, validator->pool, 4);
				
		array_append(&cmd_reg->instanced_tags, &reg, 1);
	}
}

static void sieve_validator_register_unknown_tag
(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg, 
	const char *tag) 
{
	_sieve_validator_register_tag(validator, cmd_reg, &_unknown_tag, tag, 0);
}

static const struct sieve_argument *sieve_validator_find_tag
(struct sieve_validator *valdtr, struct sieve_command_context *cmd, 
	struct sieve_ast_argument *arg, int *id_code) 
{
	struct sieve_command_registration *cmd_reg = cmd->cmd_reg;
	const char *tag = sieve_ast_argument_tag(arg);
	unsigned int i;
			
	if ( id_code != NULL )
		*id_code = 0;
	
	/* First check normal tags */
	if ( array_is_created(&cmd_reg->normal_tags) ) {
		for ( i = 0; i < array_count(&cmd_reg->normal_tags); i++ ) {
			struct sieve_tag_registration * const *reg =
				array_idx(&cmd_reg->normal_tags, i);

			if ( (*reg)->tag != NULL && strcasecmp((*reg)->identifier,tag) == 0) {
				if ( id_code != NULL )				
					*id_code = (*reg)->id_code;

				return (*reg)->tag;
			}
		}
	}	
  
	/* Not found so far, try the instanced tags */
	if ( array_is_created(&cmd_reg->instanced_tags) ) {
		for ( i = 0; i < array_count(&cmd_reg->instanced_tags); i++ ) {
			struct sieve_tag_registration * const *reg = 
				array_idx(&cmd_reg->instanced_tags, i);
  	
			if ( (*reg)->tag != NULL && 
				(*reg)->tag->is_instance_of(valdtr, cmd, arg) ) {
				if ( id_code != NULL )
					*id_code = (*reg)->id_code;
				
				return (*reg)->tag;
			}
		}
	}
	
	return NULL;
}

static const struct sieve_argument *sieve_validator_find_tag_by_identifier
(struct sieve_validator *valdtr, struct sieve_command_context *cmd, 
	const char *tag) 
{
	struct sieve_ast_argument *arg;

	/* Construct dummy argument */
	arg = t_new(struct sieve_ast_argument, 1);
	arg->type = SAAT_TAG;
	arg->_value.tag = tag; 

	return sieve_validator_find_tag(valdtr, cmd, arg, NULL);  
}

/* 
 * Extension support 
 */

const struct sieve_extension *sieve_validator_extension_load
(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	string_t *ext_name) 
{
	const struct sieve_extension *ext;
	const char *name = str_c(ext_name);

	if ( str_len(ext_name) > 128 ) {
		sieve_command_validate_error(validator, cmd, 
			"unsupported sieve capability '%s' (name is impossibly long)", 
			str_sanitize(name, 128));
		return NULL;
	}

	ext = sieve_extension_get_by_name(name); 
	
	if ( ext == NULL ) {
		sieve_command_validate_error(validator, cmd, 
			"unsupported sieve capability '%s'", name);
		return NULL;
	}
	
	sieve_ast_extension_link(validator->ast, ext);

	if ( ext->validator_load != NULL && !ext->validator_load(validator) ) {
		sieve_command_validate_error(validator, cmd, 
			"failed to load sieve capability '%s'", ext->name);
		return NULL;
	}
	
	return ext;
}

void sieve_validator_extension_register
(struct sieve_validator *valdtr, 
	const struct sieve_validator_extension *val_ext, void *context)
{
	struct sieve_validator_extension_reg reg = { val_ext, context };
	int ext_id = SIEVE_EXT_ID(val_ext->ext);

	if ( ext_id < 0 ) return;
	
	array_idx_set(&valdtr->extensions, (unsigned int) ext_id, &reg);	
}

void sieve_validator_extension_set_context
(struct sieve_validator *valdtr, const struct sieve_extension *ext, 
	void *context)
{
	struct sieve_validator_extension_reg reg = { NULL, context };
	int ext_id = SIEVE_EXT_ID(ext);

	if ( ext_id < 0 ) return;
	
	array_idx_set(&valdtr->extensions, (unsigned int) ext_id, &reg);	
}

void *sieve_validator_extension_get_context
(struct sieve_validator *valdtr, const struct sieve_extension *ext) 
{
	int ext_id = SIEVE_EXT_ID(ext);
	const struct sieve_validator_extension_reg *reg;

	if  ( ext_id < 0 || ext_id >= (int) array_count(&valdtr->extensions) )
		return NULL;
	
	reg = array_idx(&valdtr->extensions, (unsigned int) ext_id);		

	return reg->context;
}

/* 
 * Overriding the default literal arguments
 */

void sieve_validator_argument_override
(struct sieve_validator *validator, enum sieve_argument_type type, 
	const struct sieve_argument *argument)
{
	struct sieve_default_argument *arg;
	
	if ( validator->default_arguments[type].argument != NULL ) {
		arg = p_new(validator->pool, struct sieve_default_argument, 1);
		*arg = validator->default_arguments[type];	
		
		validator->default_arguments[type].overrides = arg;
	}
	
	validator->default_arguments[type].argument = argument;
}

static bool sieve_validator_argument_default_activate
(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_default_argument *defarg, struct sieve_ast_argument *arg)
{
	bool result = TRUE;
	struct sieve_default_argument *prev_defarg;
	
	prev_defarg = validator->current_defarg;
	validator->current_defarg = defarg;
	
	arg->argument = defarg->argument;
	if (defarg->argument != NULL && defarg->argument->validate != NULL )
		result = defarg->argument->validate(validator, &arg, cmd); 
		
	validator->current_defarg = prev_defarg;	
		
	return result;
}

bool sieve_validator_argument_activate_super
(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	struct sieve_ast_argument *arg, bool constant ATTR_UNUSED)
{
	struct sieve_default_argument *defarg;

	if ( validator->current_defarg == NULL ||	
		validator->current_defarg->overrides == NULL )
		return FALSE;
	
	if ( validator->current_defarg->overrides->argument == &string_argument ) {
		switch ( validator->current_defarg_type) {
		case SAT_CONST_STRING:
			if ( !validator->current_defarg_constant ) {
				validator->current_defarg_type = SAT_VAR_STRING;
				defarg = &validator->default_arguments[SAT_VAR_STRING];
			} else
				defarg = validator->current_defarg->overrides;
			break;
		case SAT_VAR_STRING:
			defarg = validator->current_defarg->overrides;
			break;
		default:
			return FALSE;
		}
	} else
		defarg = validator->current_defarg->overrides;
	
	return sieve_validator_argument_default_activate
		(validator, cmd, defarg, arg);
}

/* 
 * Argument Validation API 
 */

bool sieve_validator_argument_activate
(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	struct sieve_ast_argument *arg, bool constant)
{
	struct sieve_default_argument *defarg;
	
	switch ( sieve_ast_argument_type(arg) ) {
	case SAAT_NUMBER:	
		validator->current_defarg_type = SAT_NUMBER;
		break;
	case SAAT_STRING:
		validator->current_defarg_type = SAT_CONST_STRING;
		break;
	case SAAT_STRING_LIST:
		validator->current_defarg_type = SAT_STRING_LIST;
		break;
	default:
		return FALSE;
	}

	validator->current_defarg_constant = constant;
	defarg = &validator->default_arguments[validator->current_defarg_type];

	if ( !constant && defarg->argument == &string_argument ) {
		validator->current_defarg_type = SAT_VAR_STRING;
		defarg = &validator->default_arguments[SAT_VAR_STRING];
	}
	
	return sieve_validator_argument_default_activate(validator, cmd, defarg, arg);
}

bool sieve_validate_positional_argument
(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *arg, const char *arg_name, unsigned int arg_pos,
	enum sieve_ast_argument_type req_type)
{
	if ( sieve_ast_argument_type(arg) != req_type && 
		(sieve_ast_argument_type(arg) != SAAT_STRING || 
			req_type != SAAT_STRING_LIST) ) 
	{
		sieve_argument_validate_error(validator, arg, 
			"the %s %s expects %s as argument %d (%s), but %s was found", 
			cmd->command->identifier, sieve_command_type_name(cmd->command), 
			sieve_ast_argument_type_name(req_type),
			arg_pos, arg_name, sieve_ast_argument_name(arg));
		return FALSE; 
	}
	
	return TRUE;
}

bool sieve_validate_tag_parameter
(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *tag, struct sieve_ast_argument *param,
	enum sieve_ast_argument_type req_type)
{
	if ( sieve_ast_argument_type(param) != req_type && 
		(sieve_ast_argument_type(param) != SAAT_STRING || 
			req_type != SAAT_STRING_LIST) ) 
	{
		sieve_argument_validate_error(validator, param, 
			"the :%s tag for the %s %s requires %s as parameter, "
			"but %s was found", sieve_ast_argument_tag(tag), 
			cmd->command->identifier, sieve_command_type_name(cmd->command),
			sieve_ast_argument_type_name(req_type),	sieve_ast_argument_name(param));
		return FALSE;
	}

	param->arg_id_code = tag->arg_id_code;

	return sieve_validator_argument_activate(validator, cmd, param, FALSE);
}

/* 
 * Command argument validation 
 */

static bool sieve_validate_command_arguments
(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	int arg_count = cmd->command->positional_arguments;
	int real_count = 0;
	struct sieve_ast_argument *arg;
	struct sieve_command_registration *cmd_reg = cmd->cmd_reg;

	/* Validate any tags that might be present */
	arg = sieve_ast_argument_first(cmd->ast_node);
		
	/* Visit tagged and optional arguments */
	while ( sieve_ast_argument_type(arg) == SAAT_TAG ) {
		int id_code;
		struct sieve_ast_argument *parg; 
		const struct sieve_argument *tag = 
			sieve_validator_find_tag(validator, cmd, arg, &id_code);
		
		if ( tag == NULL ) {
			sieve_argument_validate_error(validator, arg, 
				"unknown tagged argument ':%s' for the %s %s "
				"(reported only once at first occurence)",
				sieve_ast_argument_tag(arg), cmd->command->identifier, 
				sieve_command_type_name(cmd->command));
			sieve_validator_register_unknown_tag
				(validator, cmd_reg, sieve_ast_argument_tag(arg));
			return FALSE;					
		}
		
		/* Check whether previously tagged as unknown */
		if ( tag->identifier != NULL && *(tag->identifier) == '\0' ) 
			return FALSE;
		
		/* Assign the tagged argument type to the ast for later reference 
		 * (in generator) 
		 */
		arg->argument = tag;
		arg->arg_id_code = id_code;  
			
		/* Scan backwards for any duplicates */
		parg = sieve_ast_argument_prev(arg);
		while ( parg != NULL ) {
			if ( (sieve_ast_argument_type(parg) == SAAT_TAG && parg->argument == tag) 
				|| (id_code > 0 && parg->arg_id_code == id_code) ) 
			{
				const char *tag_id = sieve_ast_argument_tag(arg);
				const char *tag_desc =
					strcmp(tag->identifier, tag_id) != 0 ?
					t_strdup_printf("%s argument (:%s)", tag->identifier, tag_id) : 
					t_strdup_printf(":%s argument", tag->identifier); 	 
				
				sieve_argument_validate_error(validator, arg, 
					"encountered duplicate %s for the %s %s",
					tag_desc, cmd->command->identifier, 
					sieve_command_type_name(cmd->command));
					
				return FALSE;	
			}
			
			parg = sieve_ast_argument_prev(parg);
		}
		
		/* Call the validation function for the tag (if present)
		 *   Fail if the validation fails:
		 *     Let's not whine multiple	times about a single command having multiple 
		 *     bad arguments...
		 */ 
		if ( tag->validate != NULL ) { 
			if ( !tag->validate(validator, &arg, cmd) ) 
				return FALSE;
		} else
			arg = sieve_ast_argument_next(arg);
	} 
	
	/* Remaining arguments should be positional (tags are not allowed here) */
	cmd->first_positional = arg;
	
	while ( arg != NULL ) {
		if ( sieve_ast_argument_type(arg) == SAAT_TAG ) {
			sieve_argument_validate_error(validator, arg, 
				"encountered an unexpected tagged argument ':%s' "
				"while validating positional arguments for the %s %s",
				sieve_ast_argument_tag(arg), cmd->command->identifier, 
				sieve_command_type_name(cmd->command));
			return FALSE;
		}
		
		real_count++;
	 
		arg = sieve_ast_argument_next(arg);
	}
	
	/* Check the required count versus the real number of arguments */
	if ( arg_count >= 0 && real_count != arg_count ) {
		sieve_command_validate_error(validator, cmd, 
			"the %s %s requires %d positional argument(s), but %d is/are specified",
			cmd->command->identifier, sieve_command_type_name(cmd->command), 
			arg_count, real_count);
		return FALSE;
	}
	
	/* Call initial validation for persistent arguments */
	if ( array_is_created(&cmd_reg->persistent_tags) ) {
  		unsigned int i;
  	
		for ( i = 0; i < array_count(&cmd_reg->persistent_tags); i++ ) {
			struct sieve_tag_registration * const *reg = 
	  			array_idx(&cmd_reg->persistent_tags, i);
			const struct sieve_argument *tag = (*reg)->tag;
  
			if ( tag != NULL && tag->validate_persistent != NULL ) { /* To be sure */
				if ( !tag->validate_persistent(validator, cmd) )
	  				return FALSE;
			}
		}
	}

	return TRUE;
}

static bool sieve_validate_arguments_context
(struct sieve_validator *validator, struct sieve_command_context *cmd)
{ 
	struct sieve_ast_argument *arg = 
		sieve_command_first_argument(cmd);
	
	while ( arg != NULL ) {
		const struct sieve_argument *argument = arg->argument;
		
		if ( argument != NULL && argument->validate_context != NULL ) { 
			if ( !argument->validate_context(validator, arg, cmd) ) 
				return FALSE;
		}
		
		arg = sieve_ast_argument_next(arg);
	}

	return TRUE;
}
 
/* 
 * Command Validation API 
 */ 
                 
static bool sieve_validate_command_subtests
(struct sieve_validator *valdtr, struct sieve_command_context *cmd, 
	const unsigned int count) 
{
	switch ( count ) {
	
	case 0:
	 	if ( sieve_ast_test_count(cmd->ast_node) > 0 ) {
			/* Unexpected command specified */
			enum sieve_command_type ctype = SCT_NONE;
			struct sieve_command_registration *cmd_reg;
			struct sieve_ast_node *test = sieve_ast_test_first(cmd->ast_node);

			cmd_reg = sieve_validator_find_command_registration
				(valdtr, test->identifier);
	
			/* First check what we are dealing with */
			if ( cmd_reg != NULL && cmd_reg->command != NULL )
				ctype = cmd_reg->command->type;

			switch ( ctype ) {
			case SCT_TEST: /* Spurious test */
				sieve_command_validate_error(valdtr, cmd, 
					"the %s %s accepts no sub-tests, but tests are specified", 
					cmd->command->identifier, sieve_command_type_name(cmd->command));
				break;

			case SCT_NONE: /* Unknown command */

				/* Is it perhaps a tag for which the ':' was omitted ? */
				if ( 	sieve_validator_find_tag_by_identifier
					(valdtr, cmd, test->identifier) != NULL ) {
					sieve_command_validate_error(valdtr, cmd, 
						"missing colon ':' before ':%s' tag in %s %s", test->identifier, 
						cmd->command->identifier, sieve_command_type_name(cmd->command));
					break;
				} 
				/* Fall through */
			
			case SCT_COMMAND:
				sieve_command_validate_error(valdtr, cmd, 
					"missing semicolon ';' after %s %s", 
					cmd->command->identifier, sieve_command_type_name(cmd->command));
				break;
			}
			return FALSE;
		}
		break;
	case 1:
		if ( sieve_ast_test_count(cmd->ast_node) == 0 ) {
			sieve_command_validate_error(valdtr, cmd, 
				"the %s %s requires one sub-test, but none is specified", 
				cmd->command->identifier, sieve_command_type_name(cmd->command));
				
			return FALSE;
			
		} else if ( sieve_ast_test_count(cmd->ast_node) > 1 || 
			cmd->ast_node->test_list ) {
			
			sieve_command_validate_error(valdtr, cmd, 
				"the %s %s requires one sub-test, but a list of tests is specified", 
				cmd->command->identifier, sieve_command_type_name(cmd->command));
				
			return FALSE;
		}
		break;
		
	default:
		if ( sieve_ast_test_count(cmd->ast_node) == 0 ) {
			sieve_command_validate_error(valdtr, cmd, 
				"the %s %s requires a list of sub-tests, but none is specified", 
				cmd->command->identifier, sieve_command_type_name(cmd->command));
			
			return FALSE;
			
		} else if ( sieve_ast_test_count(cmd->ast_node) == 1 && 
			!cmd->ast_node->test_list ) {
			
			sieve_command_validate_error(valdtr, cmd, 
				"the %s %s requires a list of sub-tests, "
				"but a single test is specified", 
				cmd->command->identifier, sieve_command_type_name(cmd->command) );
			
			return FALSE;
		}
		break;		
	}

	return TRUE;
}

static bool sieve_validate_command_block
(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	bool block_allowed, bool block_required) 
{
	i_assert( cmd->ast_node->type == SAT_COMMAND );
	
	if ( block_required ) {
		if ( !cmd->ast_node->block ) {
			sieve_command_validate_error(validator, cmd, 
				"the %s command requires a command block, but it is missing", 
				cmd->command->identifier);
			
			return FALSE;
		}
	} else if ( !block_allowed && cmd->ast_node->block ) {
		sieve_command_validate_error(validator, cmd, 
			"the %s command does not accept a command block, "
			"but one is specified anyway", 
			cmd->command->identifier );
		
		return FALSE;
	}
	
	return TRUE;
} 

/* 
 * AST Validation 
 */

static bool sieve_validate_test_list
	(struct sieve_validator *validator, struct sieve_ast_node *test_list); 
static bool sieve_validate_block
	(struct sieve_validator *validator, struct sieve_ast_node *block);
static bool sieve_validate_command
	(struct sieve_validator *validator, struct sieve_ast_node *cmd_node);
	
static bool sieve_validate_command
(struct sieve_validator *valdtr, struct sieve_ast_node *cmd_node) 
{
	enum sieve_ast_type ast_type = sieve_ast_node_type(cmd_node);
	bool result = TRUE;
	struct sieve_command_registration *cmd_reg;
	const struct sieve_command *command = NULL;
	
	i_assert( ast_type == SAT_TEST || ast_type == SAT_COMMAND );
	
	/* Verify the command specified by this node */
	
	cmd_reg = sieve_validator_find_command_registration
		(valdtr, cmd_node->identifier);
	
	if ( cmd_reg != NULL && cmd_reg->command != NULL ) {
		command = cmd_reg->command;

		/* Identifier = "" when the command was previously marked as unknown */
		if ( *(command->identifier) != '\0' ) {
			if ( (command->type == SCT_COMMAND && ast_type == SAT_TEST) || 
				(command->type == SCT_TEST && ast_type == SAT_COMMAND) ) 
			{
				sieve_validator_error(
					valdtr, cmd_node->source_line, "attempted to use %s '%s' as %s", 
					sieve_command_type_name(command), cmd_node->identifier,
					sieve_ast_type_name(ast_type));
			
			 	return FALSE;
			} 
			 
			struct sieve_command_context *ctx = 
				sieve_command_context_create(cmd_node, command, cmd_reg); 
			cmd_node->context = ctx;
		
			/* If pre-validation fails, don't bother to validate further 
			 * as context might be missing and doing so is not very useful for 
			 * further error reporting anyway
			 */
			if ( command->pre_validate == NULL || 
				command->pre_validate(valdtr, ctx) ) {
		
				/* Check syntax */
				if ( !sieve_validate_command_arguments(valdtr, ctx) ) {
					result = FALSE;

					/* A missing ':' causes a tag to become a test. This can be the cause
					 * of the arguments validation failing. Therefore we must produce an
					 * error for the sub-tests as well if appropriate.
					 */
					(void)sieve_validate_command_subtests(valdtr, ctx, command->subtests);
				} else if (
					!sieve_validate_command_subtests
 						(valdtr, ctx, command->subtests) || 
 					(ast_type == SAT_COMMAND && !sieve_validate_command_block
 						(valdtr, ctx, command->block_allowed, 
 							command->block_required)) ) {
 					result = FALSE;
 				} else {
					/* Call command validation function if specified */
					if ( command->validate != NULL )
						result = command->validate(valdtr, ctx) && result;
				}
			} else
				result = FALSE;
				
			result = result && sieve_validate_arguments_context(valdtr, ctx);
				
		} else 
			result = FALSE;
				
	} else {
		sieve_validator_error(
			valdtr, cmd_node->source_line, 
			"unknown %s '%s' (only reported once at first occurence)", 
			sieve_ast_type_name(ast_type), cmd_node->identifier);
			
		sieve_validator_register_unknown_command(valdtr, cmd_node->identifier);
		
		result = FALSE;
	}
	
	/*  
	 * Descend further into the AST 
	 */
	
	if ( command != NULL ) {
		/* Tests */
		if ( command->subtests > 0 && 
			(result || sieve_errors_more_allowed(valdtr->ehandler)) )
			result = sieve_validate_test_list(valdtr, cmd_node) && result;

		/* Command block */
		if ( command->block_allowed && ast_type == SAT_COMMAND && 
			(result || sieve_errors_more_allowed(valdtr->ehandler)) )
			result = sieve_validate_block(valdtr, cmd_node) && result;
	}
	
	return result;
}

static bool sieve_validate_test_list
(struct sieve_validator *valdtr, struct sieve_ast_node *test_list) 
{
	bool result = TRUE;
	struct sieve_ast_node *test;

	test = sieve_ast_test_first(test_list);
	while ( test != NULL && (result || 
		sieve_errors_more_allowed(valdtr->ehandler))) {	
		result = sieve_validate_command(valdtr, test) && result;	
		test = sieve_ast_test_next(test);
	}		
	
	return result;
}

static bool sieve_validate_block
(struct sieve_validator *valdtr, struct sieve_ast_node *block) 
{
	bool result = TRUE;
	struct sieve_ast_node *command, *next;

	T_BEGIN {	
		command = sieve_ast_command_first(block);
		while ( command != NULL && (result || 
			sieve_errors_more_allowed(valdtr->ehandler)) ) {	
			
			next = sieve_ast_command_next(command);
			result = sieve_validate_command(valdtr, command) && result;	
			command = next;
		}		
	} T_END;
	
	return result;
}

bool sieve_validator_run(struct sieve_validator *validator) {	
	return sieve_validate_block(validator, sieve_ast_root(validator->ast));
}

/*
 * Validator object registry
 */

struct sieve_validator_object_registry {
	struct sieve_validator *validator;
	ARRAY_DEFINE(registrations, const struct sieve_object *);
};

struct sieve_validator_object_registry *sieve_validator_object_registry_get
(struct sieve_validator *validator, const struct sieve_extension *ext)
{
	return (struct sieve_validator_object_registry *) 
		sieve_validator_extension_get_context(validator, ext);
}

void sieve_validator_object_registry_add
(struct sieve_validator_object_registry *regs, 
	const struct sieve_object *object) 
{
    array_append(&regs->registrations, &object, 1);
}

const struct sieve_object *sieve_validator_object_registry_find
(struct sieve_validator_object_registry *regs, const char *identifier) 
{
	unsigned int i;

	for ( i = 0; i < array_count(&regs->registrations); i++ ) {
		const struct sieve_object * const *obj = array_idx(&regs->registrations, i);

		if ( strcasecmp((*obj)->identifier, identifier) == 0)
			return *obj;
	}

	return NULL;
}

struct sieve_validator_object_registry *sieve_validator_object_registry_create
(struct sieve_validator *validator)
{
	pool_t pool = validator->pool;
	struct sieve_validator_object_registry *regs = 
		p_new(pool, struct sieve_validator_object_registry, 1);
	
	/* Setup registry */        
	p_array_init(&regs->registrations, validator->pool, 4);

	regs->validator = validator;

	return regs;
}

struct sieve_validator_object_registry *sieve_validator_object_registry_init
(struct sieve_validator *validator, const struct sieve_extension *ext)
{
	struct sieve_validator_object_registry *regs = 
		sieve_validator_object_registry_create(validator);
	
	sieve_validator_extension_set_context(validator, ext, regs);
	return regs;
}


