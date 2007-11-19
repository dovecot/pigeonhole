#include "lib.h"
#include "mempool.h"
#include "hash.h"

#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-extensions.h"

#include "sieve-comparators.h"
#include "sieve-address-parts.h"

/* Context/Semantics checker implementation */

struct sieve_validator {
	pool_t pool;
	struct sieve_ast *ast;
	
	struct sieve_error_handler *ehandler;
	
	/* Registries */
	struct hash_table *commands;
	
	ARRAY_DEFINE(ext_contexts, void);
};

/* Predeclared statics */

static void sieve_validator_register_core_commands(struct sieve_validator *validator);
static void sieve_validator_register_core_tests(struct sieve_validator *validator);

/* Error management */

void sieve_validator_warning(struct sieve_validator *validator, struct sieve_ast_node *node, const char *fmt, ...) 
{ 
	va_list args;
	va_start(args, fmt);
	
	sieve_vwarning(validator->ehandler, sieve_ast_node_line(node), fmt, args); 
	
	va_end(args);
}
 
void sieve_validator_error(struct sieve_validator *validator, struct sieve_ast_node *node, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	
	sieve_verror(validator->ehandler, sieve_ast_node_line(node), fmt, args); 
	
	va_end(args);
}

struct sieve_validator *sieve_validator_create(struct sieve_ast *ast, struct sieve_error_handler *ehandler) 
{
	unsigned int i;
	pool_t pool;
	struct sieve_validator *validator;
	
	pool = pool_alloconly_create("sieve_validator", 4096);	
	validator = p_new(pool, struct sieve_validator, 1);
	validator->pool = pool;
	validator->ehandler = ehandler;
	
	validator->ast = ast;	
	sieve_ast_ref(ast);

	/* Setup storage for extension contexts */		
	array_create(&validator->ext_contexts, pool, sizeof(void *), 
		sieve_extensions_get_count());
		
	/* Pre-load core language features implemented as 'extensions' */
	for ( i = 0; i < sieve_preloaded_extensions_count; i++ ) {
		const struct sieve_extension *ext = sieve_preloaded_extensions[i];
		
		if ( ext->validator_load != NULL )
			(void)ext->validator_load(validator);		
	}

	/* Setup command registry */
	validator->commands = hash_create
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
	sieve_validator_register_core_commands(validator);
	sieve_validator_register_core_tests(validator);

	return validator;
}

void sieve_validator_free(struct sieve_validator *validator) 
{
	hash_destroy(&validator->commands);
	
	sieve_ast_unref(&validator->ast);
	pool_unref(&(validator->pool));
}

inline pool_t sieve_validator_pool(struct sieve_validator *validator)
{
	return validator->pool;
}

/* Command registry */

struct sieve_tag_registration;

struct sieve_command_registration {
	const struct sieve_command *command;
	
	struct hash_table *tags;
	ARRAY_DEFINE(instanced_tags, struct sieve_tag_registration *); 
};

/* Dummy function */
static bool _cmd_unknown_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_command_context *cmd ATTR_UNUSED) 
{
	i_unreached();
	return FALSE;
}

static const struct sieve_command unknown_command = 
	{ "", SCT_COMMAND, NULL, _cmd_unknown_validate, NULL, NULL };
static const struct sieve_command unknown_test = 
	{ "", SCT_TEST, NULL, _cmd_unknown_validate, NULL, NULL };

static void sieve_validator_register_core_tests(struct sieve_validator *validator) 
{
	unsigned int i;
	
	for ( i = 0; i < sieve_core_tests_count; i++ ) {
		sieve_validator_register_command(validator, &sieve_core_tests[i]); 
	}
}

static void sieve_validator_register_core_commands(struct sieve_validator *validator) 
{
	unsigned int i;
	
	for ( i = 0; i < sieve_core_commands_count; i++ ) {
		sieve_validator_register_command(validator, &sieve_core_commands[i]); 
	}
}

static void _sieve_validator_register_command
	(struct sieve_validator *validator, const struct sieve_command *command,
	const char *identifier) 
{
	struct sieve_command_registration *record = 
		p_new(validator->pool, struct sieve_command_registration, 1);
	record->command = command;
	record->tags = NULL;
	hash_insert(validator->commands, (void *) identifier, (void *) record);
	
	if ( command->registered != NULL ) {
		command->registered(validator, record);
	}
}

void sieve_validator_register_command
	(struct sieve_validator *validator, const struct sieve_command *command) 
{
	_sieve_validator_register_command(validator, command, command->identifier);
}

static void sieve_validator_register_unknown_command
	(struct sieve_validator *validator, const char *command) 
{
	_sieve_validator_register_command(validator, &unknown_command, command);		
}

static void sieve_validator_register_unknown_test
	(struct sieve_validator *validator, const char *test) 
{		
	_sieve_validator_register_command(validator, &unknown_test, test);		
}

static struct sieve_command_registration *sieve_validator_find_command_registration
		(struct sieve_validator *validator, const char *command) 
{
  return 	(struct sieve_command_registration *) hash_lookup(validator->commands, command);
}

static const struct sieve_command *
	sieve_validator_find_command(struct sieve_validator *validator, const char *command) 
{
  struct sieve_command_registration *record = 
  	sieve_validator_find_command_registration(validator, command);
  
  return ( record == NULL ? NULL : record->command );
}

/* Per-command tag/argument registry */

struct sieve_tag_registration {
	const struct sieve_argument *tag;
	
	unsigned int id_code;
};

static bool _unknown_tag_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_ast_argument **arg ATTR_UNUSED, 
	struct sieve_command_context *tst ATTR_UNUSED)
{
	i_unreached();
	return FALSE;
}

static const struct sieve_argument _unknown_tag = 
	{ "", NULL, _unknown_tag_validate, NULL };

static void _sieve_validator_register_tag
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg, 
	const struct sieve_argument *tag, const char *identifier, unsigned int id_code) 
{
	struct sieve_tag_registration *reg;
	
	reg = p_new(validator->pool, struct sieve_tag_registration, 1);
	reg->tag = tag;
	reg->id_code = id_code;
	
	if ( cmd_reg->tags == NULL ) {
		cmd_reg->tags = hash_create
			(validator->pool, validator->pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
	}
	
	hash_insert(cmd_reg->tags, (void *) identifier, (void *) reg);
}

void sieve_validator_register_tag
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg, 
	const struct sieve_argument *tag, unsigned int id_code) 
{
	if ( tag->is_instance_of == NULL )
		_sieve_validator_register_tag(validator, cmd_reg, tag, tag->identifier, id_code);
	else {
		struct sieve_tag_registration *reg = 
			p_new(validator->pool, struct sieve_tag_registration, 1);
		reg->tag = tag;
		reg->id_code = id_code;

		if ( !array_is_created(&cmd_reg->instanced_tags) ) 
				p_array_init(&cmd_reg->instanced_tags, validator->pool, 1);
				
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
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg, 
		const char *tag, unsigned int *id_code) 
{
	unsigned int i;
	const struct sieve_tag_registration *reg;
	
	*id_code = 0;
	
	if ( cmd_reg->tags != NULL ) {
		reg = (const struct sieve_tag_registration *) 
			hash_lookup(cmd_reg->tags, tag);
	
		if ( reg != NULL ) {
			*id_code = reg->id_code;
  		return reg->tag; 
  	}
  }
  
  /* Not found so far, try the instanced tags */
  if ( array_is_created(&cmd_reg->instanced_tags) ) {
	  for ( i = 0; i < array_count(&cmd_reg->instanced_tags); i++ ) {
	  	struct sieve_tag_registration * const *reg = 
	  		array_idx(&cmd_reg->instanced_tags, i);
  	
	  	if ( (*reg)->tag != NULL && (*reg)->tag->is_instance_of(validator, tag) ) {
	  		*id_code = (*reg)->id_code;
	  		return (*reg)->tag;
	  	}
	  }
	}
	
	return NULL;
}

/* Extension support */

int sieve_validator_extension_load
	(struct sieve_validator *validator, struct sieve_command_context *cmd, 
		const char *ext_name) 
{
	const struct sieve_extension *ext;
	int ext_id = sieve_extension_get_by_name(ext_name, &ext); 
	
	if ( ext_id < 0 ) {
		sieve_command_validate_error(validator, cmd, 
			"unsupported sieve capability '%s'", ext_name);
		return -1;
	}

	if ( ext->validator_load != NULL && !ext->validator_load(validator) ) {
		sieve_command_validate_error(validator, cmd, 
			"failed to load sieve capability '%s'", ext->name);
		return -1;
	}
	
	i_info("loaded extension '%s'", ext->name);
	return ext_id;
}

inline void sieve_validator_extension_set_context(struct sieve_validator *validator, int ext_id, void *context)
{
	array_idx_set(&validator->ext_contexts, (unsigned int) ext_id, context);	
}

inline const void *sieve_validator_extension_get_context(struct sieve_validator *validator, int ext_id) 
{
	if  ( ext_id < 0 || ext_id > (int) array_count(&validator->ext_contexts) )
		return NULL;
	
	return array_idx(&validator->ext_contexts, (unsigned int) ext_id);		
}

/* Argument Validation API */

bool sieve_validate_positional_argument
	(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *arg, const char *arg_name, unsigned int arg_pos,
	enum sieve_ast_argument_type req_type)
{
	if ( sieve_ast_argument_type(arg) != req_type && 
		(sieve_ast_argument_type(arg) != SAAT_STRING || 
			req_type != SAAT_STRING_LIST) ) 
	{
		sieve_command_validate_error(validator, cmd, 
			"the %s %s expects %s as argument %d (%s), but %s was found", 
			cmd->command->identifier, sieve_command_type_name(cmd->command), 
			sieve_ast_argument_type_name(req_type),
			arg_pos, arg_name, sieve_ast_argument_name(arg));
		return FALSE; 
	}
	
	return TRUE;
}

void sieve_validator_argument_activate
	(struct sieve_validator *validator ATTR_UNUSED, struct sieve_ast_argument *arg)
{
	switch ( sieve_ast_argument_type(arg) ) {
	case SAAT_NUMBER:
		arg->argument = &number_argument;
		break;
	case SAAT_STRING:
		arg->argument = &string_argument;
		break;
	case SAAT_STRING_LIST:
		arg->argument = &string_list_argument;
		break;
	case SAAT_TAG:
		i_error("!!BUG!!: sieve_validator_argument_activate: cannot activate tagged argument.");
		break;
	default:
		break;
	}
}

/* Test validation API */

bool sieve_validate_command_arguments
	(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	 const unsigned int count) 
{
	struct sieve_ast_argument *arg;
	unsigned int real_count = 0;
	struct sieve_command_registration *cmd_reg = NULL;
	
	/* Validate any tags that might be present */\
	
	arg = sieve_ast_argument_first(cmd->ast_node);
	
	/* Get the command registration to get access to its tag registry */
	if ( sieve_ast_argument_type(arg) == SAAT_TAG ) {
		cmd_reg = sieve_validator_find_command_registration(validator, cmd->command->identifier);
		
		if ( cmd_reg == NULL ) {
			sieve_command_validate_error(
				validator, cmd, 
				"!!BUG!!: the '%s' %s seemed to be known before, "
				"but somehow its registration got lost",
				cmd->command->identifier, sieve_command_type_name(cmd->command)
			);
			i_error("BUG: the '%s' %s seemed to be known before, "
				"but somehow its registration got lost",
				cmd->command->identifier, sieve_command_type_name(cmd->command)
			);
			return FALSE; 
		}
	}
	
	/* Parse tagged and optional arguments */
	while ( sieve_ast_argument_type(arg) == SAAT_TAG ) {
		unsigned int id_code;
		const struct sieve_argument *tag = 
			sieve_validator_find_tag
				(validator, cmd_reg, sieve_ast_argument_tag(arg), &id_code);
		
		if ( tag == NULL ) {
			sieve_command_validate_error(validator, cmd, 
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
		
		/* Assign the tagged argument type to the ast for later reference (in generator) */
		arg->argument = tag;
		arg->arg_id_code = id_code;
		
		/* Call the validation function for the tag (if present)
		 *   Fail if the validation fails.
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
			sieve_command_validate_error(validator, cmd, 
				"encountered an unexpected tagged argument ':%s' while validating positional arguments for the %s %s",
				sieve_ast_argument_tag(arg), cmd->command->identifier, sieve_command_type_name(cmd->command));
			return FALSE;
		}
		
		real_count++;
	 
		arg = sieve_ast_argument_next(arg);
	}
	
	/* Check the required count versus the real number of arguments */
	if ( real_count != count ) {
		sieve_command_validate_error(validator, cmd, 
			"the %s %s requires %d positional argument(s), but %d is/are specified",
			cmd->command->identifier, sieve_command_type_name(cmd->command), count, real_count);
		return FALSE;
	}

	return TRUE;
}
 
/* Command Validation API */ 
                 
bool sieve_validate_command_subtests
	( struct sieve_validator *validator, struct sieve_command_context *cmd, const unsigned int count ) \
{
	switch ( count ) {
	
	case 0:
	 	if ( sieve_ast_test_count(cmd->ast_node) > 0 ) {
			sieve_command_validate_error
				( validator, cmd, "the %s %s accepts no sub-tests, but tests are specified anyway", 
					cmd->command->identifier, sieve_command_type_name(cmd->command) );
			return FALSE;
		}
		break;
	case 1:
		if ( sieve_ast_test_count(cmd->ast_node) == 0 ) {
			sieve_command_validate_error
				( validator, cmd, "the %s %s requires one sub-test, but none is specified", 
					cmd->command->identifier, sieve_command_type_name(cmd->command) );
			return FALSE;
		} else if ( sieve_ast_test_count(cmd->ast_node) > 1 || cmd->ast_node->test_list ) {
			sieve_command_validate_error
				( validator, cmd, "the %s %s requires one sub-test, but a list of tests is specified", 
					cmd->command->identifier, sieve_command_type_name(cmd->command) );
			return FALSE;
		}
		break;
		
	default:
		if ( sieve_ast_test_count(cmd->ast_node) == 0 ) {
			sieve_command_validate_error
				( validator, cmd, "the %s %s requires a list of sub-tests, but none is specified", 
					cmd->command->identifier, sieve_command_type_name(cmd->command) );
			return FALSE;
		} else if ( sieve_ast_test_count(cmd->ast_node) == 1 && !cmd->ast_node->test_list ) {
			sieve_command_validate_error
				( validator, cmd, "the %s %s requires a list of sub-tests, but a single test is specified", 
					cmd->command->identifier, sieve_command_type_name(cmd->command) );
			return FALSE;
		}
		break;		
	}

	return TRUE;
}

bool sieve_validate_command_block(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	bool block_allowed, bool block_required) 
{
	i_assert( cmd->ast_node->type == SAT_COMMAND );
	
	if ( block_required ) {
		if ( !cmd->ast_node->block ) {
			sieve_command_validate_error
				( validator, cmd, 
					"the %s command requires a command block, but it is missing", 
					cmd->command->identifier );
			return FALSE;
		}
	} else if ( !block_allowed && cmd->ast_node->block ) {
		sieve_command_validate_error
				( validator, cmd, 
					"the %s command does not accept a command block, but one is specified anyway", 
					cmd->command->identifier );
		return FALSE;
	}
	
	return TRUE;
} 

/* AST Validation */

static bool sieve_validate_test_list(struct sieve_validator *validator, struct sieve_ast_node *test_list); 
static bool sieve_validate_test(struct sieve_validator *validator, struct sieve_ast_node *tst_node); 
static bool sieve_validate_block(struct sieve_validator *validator, struct sieve_ast_node *block);
static bool sieve_validate_command(struct sieve_validator *validator, struct sieve_ast_node *cmd_node);

static bool sieve_validate_test(struct sieve_validator *validator, struct sieve_ast_node *tst_node) 
{
	bool result = TRUE;
	const struct sieve_command *test;
	
	i_assert( sieve_ast_node_type(tst_node) == SAT_TEST );
	
	test = sieve_validator_find_command(validator, tst_node->identifier);
	
	if ( test != NULL ) {
		/* Identifier = "" when the command was previously marked as unknown */
		if ( *(test->identifier) != '\0' ) {
			if ( test->type != SCT_TEST ) {
				sieve_validator_error(
					validator, tst_node, 
					"attempted to use command '%s' as test", tst_node->identifier);
			 	result = FALSE;
			} else {
				struct sieve_command_context *ctx = sieve_command_context_create(tst_node, test); 
				tst_node->context = ctx;
			
				/* Call command validation function if specified or execute defaults otherwise */
				if ( test->validate != NULL )
					result = test->validate(validator, ctx) && result;
				else {
					/* Check default syntax 
	 				 *   Syntax: test
	 				 */
	 				if ( !sieve_validate_command_arguments(validator,ctx, 0) ||
	 					!sieve_validate_command_subtests(validator, ctx, 0) ) 
	 					return FALSE; 
				}
			}
		} else 
			result = FALSE;
			
	} else {
		sieve_validator_error(validator, tst_node, 
			"unknown test '%s' (only reported once at first occurence)", 
			tst_node->identifier);
		sieve_validator_register_unknown_test(validator, tst_node->identifier);
		
		result = FALSE;
	}
	
	result = sieve_validate_test_list(validator, tst_node) && result;

	return result;
}

static bool sieve_validate_test_list(struct sieve_validator *validator, struct sieve_ast_node *test_list) 
{
	bool result = TRUE;
	struct sieve_ast_node *test;

	test = sieve_ast_test_first(test_list);
	while ( test != NULL ) {	
		result = sieve_validate_test(validator, test) && result;	
		test = sieve_ast_test_next(test);
	}		
	
	return result;
}

static bool sieve_validate_command(struct sieve_validator *validator, struct sieve_ast_node *cmd_node) 
{
	bool result = TRUE;
	const struct sieve_command *command;
	
	i_assert( sieve_ast_node_type(cmd_node) == SAT_COMMAND );
	
	command = sieve_validator_find_command(validator, cmd_node->identifier);
	
	if ( command != NULL ) {
		/* Identifier = "" when the command was previously marked as unknown */
		if ( *(command->identifier) != '\0' ) {
			if ( command->type != SCT_COMMAND ) {
				sieve_validator_error(validator, cmd_node, "attempted to use test '%s' as command", cmd_node->identifier);
			 	result = FALSE;
			} else { 
				struct sieve_command_context *ctx = sieve_command_context_create(cmd_node, command); 
				cmd_node->context = ctx;
			
				/* Call command validation function if specified or execute defaults otherwise */
				if ( command->validate != NULL )
					result = command->validate(validator, ctx) && result;
				else {
					/* Check default syntax 
	 				 *   Syntax: command
	 				 */
					if ( !sieve_validate_command_arguments(validator,ctx, 0) ||
	 					!sieve_validate_command_subtests(validator, ctx, 0) || 
	 					!sieve_validate_command_block(validator, ctx, FALSE, FALSE) ) {
	 				
	 					result = FALSE;
					}
				}
			}
		} else 
			result = FALSE;
			
	} else {
		sieve_validator_error(
			validator, cmd_node, 
			"unknown command '%s' (only reported once at first occurence)", 
			cmd_node->identifier);
		sieve_validator_register_unknown_command(validator, cmd_node->identifier);
		
		result = FALSE;
	}
	
	result = sieve_validate_test_list(validator, cmd_node) && result;
	result = sieve_validate_block(validator, cmd_node) && result;
	
	return result;
}

static bool sieve_validate_block(struct sieve_validator *validator, struct sieve_ast_node *block) 
{
	bool result = TRUE;
	struct sieve_ast_node *command;

	t_push();	
	command = sieve_ast_command_first(block);
	while ( command != NULL ) {	
		result = sieve_validate_command(validator, command) && result;	
		command = sieve_ast_command_next(command);
	}		
	t_pop();
	
	return result;
}

bool sieve_validator_run(struct sieve_validator *validator) {	
	return sieve_validate_block(validator, sieve_ast_root(validator->ast));
}



