#ifndef SIEVE_COMMON_H
#define SIEVE_COMMON_H

#include "lib.h"

#include "sieve.h"
#ifndef SETTINGS_PLUGIN
#include "sieve-settings.h"
#endif

#include <sys/types.h>

/*
 * Types
 */

typedef size_t sieve_size_t;
typedef uint32_t sieve_offset_t;
typedef uint64_t sieve_number_t;

#define SIEVE_MAX_NUMBER ((sieve_number_t)-1)
#define SIEVE_PRI_NUMBER PRIu64

/*
 * Forward declarations
 */

/* sieve-error.h */
struct sieve_error_handler;

/* sieve-ast.h */
enum sieve_ast_argument_type;

struct sieve_ast;
struct sieve_ast_node;
struct sieve_ast_argument;

/* sieve-commands.h */
struct sieve_argument;
struct sieve_argument_def;
struct sieve_command;
struct sieve_command_def;
struct sieve_command_context;
struct sieve_command_registration;

/* sieve-stringlist.h */
struct sieve_stringlist;

/* sieve-code.h */
struct sieve_operation_extension;

/* sieve-lexer.h */
struct sieve_lexer;

/* sieve-parser.h */
struct sieve_parser;

/* sieve-validator.h */
struct sieve_validator;

/* sieve-generator.h */
struct sieve_jumplist;
struct sieve_generator;
struct sieve_codegen_env;

/* sieve-runtime.h */
struct sieve_runtime_env;

/* sieve-interpreter.h */
struct sieve_interpreter;

/* sieve-dump.h */
struct sieve_dumptime_env;

/* sieve-binary-dumper.h */
struct sieve_binary_dumper;

/* sieve-code-dumper.h */
struct sieve_code_dumper;

/* sieve-extension.h */
struct sieve_extension;
struct sieve_extension_def;
struct sieve_extension_objects;

/* sieve-code.h */
struct sieve_operand;
struct sieve_operand_def;
struct sieve_operand_class;
struct sieve_operation;
struct sieve_coded_stringlist;

/* sieve-binary.h */
struct sieve_binary;
struct sieve_binary_block;
struct sieve_binary_debug_writer;
struct sieve_binary_debug_reader;

/* sieve-execute.h */
struct sieve_execute;

/* sieve-objects.h */
struct sieve_object_def;
struct sieve_object;

/* sieve-comparator.h */
struct sieve_comparator;

/* sieve-match-types.h */
struct sieve_match_type;

/* sieve-match.h */
struct sieve_match_context;

/* sieve-address.h */
struct sieve_address_list;

/* sieve-address-parts.h */
struct sieve_address_part_def;
struct sieve_address_part;

/* sieve-result.h */
struct sieve_result;
struct sieve_side_effects_list;
struct sieve_result_print_env;

/* sieve-actions.h */
struct sieve_action_exec_env;
struct sieve_action;
struct sieve_action_def;
struct sieve_side_effect;
struct sieve_side_effect_def;

/* sieve-script.h */
struct sieve_script;
struct sieve_script_sequence;

/* sieve-storage.h */
struct sieve_storage_class_registry;
struct sieve_storage;

/* sieve-message.h */
struct sieve_message_context;
struct sieve_message_override;
struct sieve_message_override_def;

/* sieve-plugins.h */
struct sieve_plugin;

/* sieve.c */
struct sieve_ast *
sieve_parse(struct sieve_script *script, struct sieve_error_handler *ehandler,
	    enum sieve_error *error_code_r);
bool sieve_validate(struct sieve_ast *ast, struct sieve_error_handler *ehandler,
		    enum sieve_compile_flags flags,
		    enum sieve_error *error_code_r);

/*
 * Parent category
 */

extern struct event_category event_category_sieve;

/*
 * Sieve engine instance
 */

struct sieve_instance {
	/* Main engine pool */
	pool_t pool;

	/* System environment */
	const char *hostname;
	const char *domainname;
	const char *base_dir;
	const char *temp_dir;

	/* User environment */
	const char *username;
	const char *home_dir;

	/* Flags */
	enum sieve_flag flags;

	/* Callbacks */
	const struct sieve_callbacks *callbacks;
	void *context;

	/* Logging, events, and debug */
	struct event *event;
	bool debug;

	/* Extension registry */
	struct sieve_extension_registry *ext_reg;

	/* Storage class registry */
	struct sieve_storage_class_registry *storage_reg;

	/* Plugin modules */
	struct sieve_plugin *plugins;
	enum sieve_env_location env_location;
	enum sieve_delivery_phase delivery_phase;

	/* Settings */
	const struct sieve_settings *set;
	const struct smtp_address *user_email_implicit;
};

/*
 * Errors
 */

void sieve_error_args_init(enum sieve_error **error_code_r,
			   const char ***error_r);

void sieve_error_create_internal(enum sieve_error *error_code_r,
				 const char **error_r);
void sieve_error_create_script_not_found(const char *script_name,
					 enum sieve_error *error_code_r,
					 const char **error_r);

/*
 * Script trace log
 */

void sieve_trace_log_write_line(struct sieve_trace_log *trace_log,
				const string_t *line);

/*
 * User e-mail address
 */

const struct smtp_address *sieve_get_user_email	(struct sieve_instance *svinst);

/*
 * Postmaster address
 */

const struct message_address *
sieve_get_postmaster(const struct sieve_script_env *senv);
const struct smtp_address *
sieve_get_postmaster_smtp(const struct sieve_script_env *senv);
const char *sieve_get_postmaster_address(const struct sieve_script_env *senv);

/*
 * Home directory
 */

static inline const char *
sieve_environment_get_homedir(struct sieve_instance *svinst)
{
	const struct sieve_callbacks *callbacks = svinst->callbacks;

	if (svinst->home_dir != NULL)
		return svinst->home_dir;
	if (callbacks == NULL || callbacks->get_homedir == NULL)
		return NULL;

	return callbacks->get_homedir(svinst, svinst->context);
}

#endif
