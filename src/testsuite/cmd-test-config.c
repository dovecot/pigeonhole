/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "testsuite-common.h"
#include "testsuite-settings.h"

/*
 * Test_config command
 *
 * Syntax:   
 *   test_config (
 *     :set <setting: string> <value: string> /
 *     :unset <setting: string> /  
 *     :reload [<extension: string>] )
 */

static bool cmd_test_config_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool cmd_test_config_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

const struct sieve_command_def cmd_test_config = { 
	"test_config", 
	SCT_COMMAND, 
	0, 0, FALSE, FALSE,
	cmd_test_config_registered, 
	NULL, NULL,
	cmd_test_config_generate, 
	NULL 
};

/* 
 * Operations
 */ 
 
/* Test_message_set operation */

static bool cmd_test_config_set_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_test_config_set_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def test_config_set_operation = { 
	"TEST_CONFIG_SET",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_CONFIG_SET,
	cmd_test_config_set_operation_dump, 
	cmd_test_config_set_operation_execute 
};

/* Test_message_unset operation */

static bool cmd_test_config_unset_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_test_config_unset_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def test_config_unset_operation = { 
	"TEST_CONFIG_UNSET",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_CONFIG_UNSET,
	cmd_test_config_unset_operation_dump, 
	cmd_test_config_unset_operation_execute 
};

/* Test_message_mailbox operation */

static bool cmd_test_config_reload_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_test_config_reload_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def test_config_reload_operation = { 
	"TEST_CONFIG_RELOAD",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_CONFIG_RELOAD,
	cmd_test_config_reload_operation_dump, 
	cmd_test_config_reload_operation_execute 
};

/*
 * Compiler context data
 */

enum cmd_test_config_action {
	CONFIG_ACTION_SET,
	CONFIG_ACTION_UNSET,
	CONFIG_ACTION_RELOAD,
	CONFIG_ACTION_LAST
};

const struct sieve_operation_def *test_config_operations[] = {
	&test_config_set_operation,
	&test_config_unset_operation,
	&test_config_reload_operation
};
 
struct cmd_test_config_data {
	enum cmd_test_config_action action;
};

/* 
 * Command tags 
 */
 
static bool tag_action_is_instance_of
	(struct sieve_validator *valdtr, struct sieve_command *cmd, 
		const struct sieve_extension *ext, const char *identifier, void **data);
static bool tag_action_validate
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
		struct sieve_command *cmd);
static bool tag_action_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command *cmd);

static const struct sieve_argument_def config_action_tag = { 
	"CONFIG_ACTION",
	tag_action_is_instance_of,
	tag_action_validate, 
	NULL,	NULL,
	tag_action_generate 
};

static bool tag_action_is_instance_of
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_command *cmd, 
	const struct sieve_extension *ext ATTR_UNUSED, const char *identifier, 
	void **data)
{
	enum cmd_test_config_action action = CONFIG_ACTION_LAST; 
	struct cmd_test_config_data *ctx_data;

	if ( strcmp(identifier, "set") == 0 )
		action = CONFIG_ACTION_SET;
	else if ( strcmp(identifier, "unset") == 0 )
		action = CONFIG_ACTION_UNSET;
	else if ( strcmp(identifier, "reload") == 0 )
		action = CONFIG_ACTION_RELOAD;
	else 
		return FALSE;

	if ( data != NULL ) {
		ctx_data = p_new
			(sieve_command_pool(cmd), struct cmd_test_config_data, 1);
		ctx_data->action = action;
		*data = (void *) ctx_data;
  }		
		
	return TRUE;
}

static bool cmd_test_config_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg) 
{
	/* Register our tags */
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &config_action_tag, 0); 	

	return TRUE;
}

static bool tag_action_validate
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
	struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct cmd_test_config_data *ctx_data = 
		(struct cmd_test_config_data *) (*arg)->argument->data;

	*arg = sieve_ast_argument_next(*arg);
			
	switch ( ctx_data->action ) {
	case CONFIG_ACTION_SET:
		/* Check syntax:
		 *   :set <setting: string> <value: string>
		 */
		if ( !sieve_validate_tag_parameter
			(valdtr, cmd, tag, *arg, "setting", 1, SAAT_STRING, TRUE) ) {
			return FALSE;
		}

		tag->parameters = *arg;
		*arg = sieve_ast_argument_next(*arg);

		if ( !sieve_validate_tag_parameter
			(valdtr, cmd, tag, *arg, "value", 2, SAAT_STRING, TRUE) ) {
			return FALSE;
		}

		/* Detach parameters */
		*arg = sieve_ast_arguments_detach(tag->parameters,2);
		break;
	case CONFIG_ACTION_UNSET:
		/* Check syntax:
		 *   :unset <setting: string>
		 */
		if ( !sieve_validate_tag_parameter
			(valdtr, cmd, tag, *arg, NULL, 0, SAAT_STRING, TRUE) ) {
			return FALSE;
		}

		/* Detach parameter */
		tag->parameters = *arg;
		*arg = sieve_ast_arguments_detach(*arg,1);
		break;

	case CONFIG_ACTION_RELOAD:
		/* Check syntax:
		 *   :reload <extension: string>
		 */
		if ( !sieve_validate_tag_parameter
			(valdtr, cmd, tag, *arg, NULL, 0, SAAT_STRING, TRUE) ) {
			return FALSE;
		}

		/* Detach parameter */
		tag->parameters = *arg;
		*arg = sieve_ast_arguments_detach(*arg,1);
		break;
	default:
		i_unreached();
	}
			
	return TRUE;
}

static bool tag_action_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command *cmd)
{
	struct sieve_ast_argument *param = arg->parameters;
	struct cmd_test_config_data *ctx_data = 
		(struct cmd_test_config_data *) arg->argument->data;
			
	i_assert(ctx_data->action < CONFIG_ACTION_LAST);

	sieve_operation_emit
		(cgenv->sbin, cmd->ext, test_config_operations[ctx_data->action]);

	while ( param != NULL ) {
		if ( !sieve_generate_argument(cgenv, param, cmd) )
			return FALSE;

		param = sieve_ast_argument_next(param);
	}

	return TRUE;
}

/* 
 * Code generation 
 */

static bool cmd_test_config_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{	  	
 	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_test_config_set_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "TEST_CONFIG_SET:");
	
	sieve_code_descend(denv);
	
	return sieve_opr_string_dump(denv, address, "setting") &&
		sieve_opr_string_dump(denv, address, "value");
}

static bool cmd_test_config_unset_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "TEST_CONFIG_UNSET:");
	
	sieve_code_descend(denv);

	return 
		sieve_opr_string_dump(denv, address, "setting");
}

static bool cmd_test_config_reload_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "TEST_CONFIG_RELOAD:");
	
	sieve_code_descend(denv);

	return 
		sieve_opr_string_dump(denv, address, "extension");
}

/*
 * Intepretation
 */
 
static int cmd_test_config_set_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *setting;
	string_t *value;

	/* 
	 * Read operands 
	 */

	/* Setting */

	if ( !sieve_opr_string_read(renv, address, &setting) ) {
		sieve_runtime_trace_error(renv, "invalid setting operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Value */

	if ( !sieve_opr_string_read(renv, address, &value) ) {
		sieve_runtime_trace_error(renv, "invalid value operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/*
	 * Perform operation
	 */
		
	sieve_runtime_trace(renv, "TEST_CONFIG_SET %s = '%s'", 
		str_c(setting), str_c(value));

	testsuite_setting_set(str_c(setting), str_c(value));

	return SIEVE_EXEC_OK;
}

static int cmd_test_config_unset_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	string_t *setting;

	/* 
	 * Read operands 
	 */

	if ( !sieve_opr_string_read(renv, address, &setting) ) {
		sieve_runtime_trace_error(renv, "invalid setting operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/*
	 * Perform operation
	 */
		
	sieve_runtime_trace(renv, "TEST_CONFIG_UNSET %s", str_c(setting));

	testsuite_setting_unset(str_c(setting));

	return SIEVE_EXEC_OK;
}

static int cmd_test_config_reload_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_extension *ext;
	string_t *extension;

	/* 
	 * Read operands 
	 */

	/* Extension */

	if ( !sieve_opr_string_read(renv, address, &extension) ) {
		sieve_runtime_trace_error(renv, "invalid extension operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/*
	 * Perform operation
	 */
		
	sieve_runtime_trace(renv, "TEST_CONFIG_RELOAD [%s]", str_c(extension));

	ext = sieve_extension_get_by_name(renv->svinst, str_c(extension));
	if ( ext == NULL ) {
		testsuite_test_failf("unknown extension '%s'", str_c(extension));
		return SIEVE_EXEC_OK;
	}	

	sieve_extension_reload(ext);
	return SIEVE_EXEC_OK;
}





