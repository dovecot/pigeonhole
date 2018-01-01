/* Copyright (c) 2015-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "sieve-ext-variables.h"

#include "ext-vnd-environment-common.h"

static bool vnspc_vnd_environment_validate
	(struct sieve_validator *valdtr,
		const struct sieve_variables_namespace *nspc,
		struct sieve_ast_argument *arg, struct sieve_command *cmd,
		ARRAY_TYPE(sieve_variable_name) *var_name, void **var_data,
		bool assignment);
static bool vnspc_vnd_environment_generate
	(const struct sieve_codegen_env *cgenv,
		const struct sieve_variables_namespace *nspc,
		struct sieve_ast_argument *arg,
		struct sieve_command *cmd, void *var_data);
static bool vnspc_vnd_environment_dump_variable
	(const struct sieve_dumptime_env *denv,
		const struct sieve_variables_namespace *nspc, 
		const struct sieve_operand *oprnd, sieve_size_t *address);
static int vnspc_vnd_environment_read_variable
	(const struct sieve_runtime_env *renv,
		const struct sieve_variables_namespace *nspc,
		const struct sieve_operand *oprnd,
		sieve_size_t *address, string_t **str_r);

static const struct sieve_variables_namespace_def
environment_namespace = {
	SIEVE_OBJECT("env", &environment_namespace_operand, 0),
	.validate = vnspc_vnd_environment_validate,
	.generate = vnspc_vnd_environment_generate,
	.dump_variable = vnspc_vnd_environment_dump_variable,
	.read_variable = vnspc_vnd_environment_read_variable
};

static bool vnspc_vnd_environment_validate
(struct sieve_validator *valdtr, 
	const struct sieve_variables_namespace *nspc ATTR_UNUSED,
	struct sieve_ast_argument *arg, struct sieve_command *cmd ATTR_UNUSED,
	ARRAY_TYPE(sieve_variable_name) *var_name, void **var_data,
	bool assignment)
{
	struct sieve_ast *ast = arg->ast;
	const struct sieve_variable_name *name_elements;
	unsigned int i, count;
	const char *variable;
	string_t *name;

	/* Compose environment name from parsed variable name */
	name = t_str_new(64);
	name_elements = array_get(var_name, &count);
	i_assert(count > 1);
	for (i = 1; i < count; i++) {
		if ( name_elements[i].num_variable >= 0 ) {
			sieve_argument_validate_error(valdtr, arg,
				"vnd.dovecot.environment: invalid variable name within "
				"env namespace `env.%d': "
				"encountered numeric variable name",
				name_elements[i].num_variable);
			return FALSE;
		}
		if (str_len(name) > 0)
			str_append_c(name, '.');
		str_append_str(name, name_elements[i].identifier);
	}

	variable = str_c(name);

	if ( assignment ) {
		sieve_argument_validate_error(valdtr, arg,
			"vnd.dovecot.environment: cannot assign to environment "
			"variable `env.%s'", variable);
		return FALSE;
	}

	*var_data = (void *) p_strdup(sieve_ast_pool(ast), variable);
	return TRUE;
}

static bool vnspc_vnd_environment_generate
(const struct sieve_codegen_env *cgenv,
	const struct sieve_variables_namespace *nspc,
	struct sieve_ast_argument *arg ATTR_UNUSED,
	struct sieve_command *cmd ATTR_UNUSED, void *var_data)
{
	const struct sieve_extension *this_ext = SIEVE_OBJECT_EXTENSION(nspc);	
	const char *variable = (const char *) var_data;
	struct ext_vnd_environment_context *ext_data;

	if ( this_ext == NULL )
		return FALSE;

	ext_data = (struct ext_vnd_environment_context *) this_ext->context;

	sieve_variables_opr_namespace_variable_emit
		(cgenv->sblock, ext_data->var_ext, this_ext, &environment_namespace);
	sieve_binary_emit_cstring(cgenv->sblock, variable);

	return TRUE;
}

static bool vnspc_vnd_environment_dump_variable
(const struct sieve_dumptime_env *denv,
	const struct sieve_variables_namespace *nspc ATTR_UNUSED,
	const struct sieve_operand *oprnd, sieve_size_t *address)
{
	string_t *var_name;

	if ( !sieve_binary_read_string(denv->sblock, address, &var_name) )
		return FALSE;

	if ( oprnd->field_name != NULL )
		sieve_code_dumpf(denv, "%s: VAR ${env.%s}",
			oprnd->field_name, str_c(var_name));
	else
		sieve_code_dumpf(denv, "VAR ${env.%s}",
			str_c(var_name));

	return TRUE;
}

static int vnspc_vnd_environment_read_variable
(const struct sieve_runtime_env *renv,
	const struct sieve_variables_namespace *nspc,
	const struct sieve_operand *oprnd, sieve_size_t *address,
	string_t **str_r)
{
	const struct sieve_extension *this_ext = SIEVE_OBJECT_EXTENSION(nspc);	
	struct ext_vnd_environment_context *ectx =
		(struct ext_vnd_environment_context *) this_ext->context;
	string_t *var_name;
	const char *ext_value;

	if ( !sieve_binary_read_string(renv->sblock, address, &var_name) ) {
		sieve_runtime_trace_operand_error(renv, oprnd,
			"environment variable operand corrupt: invalid name");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	if ( str_r !=  NULL ) {
		const char *vname = str_c(var_name);

		ext_value = ext_environment_item_get_value
			(ectx->env_ext, renv, vname);

		if ( ext_value == NULL && strchr(vname, '_') != NULL) {
			char *p, *aname;

			/* Try again with '_' replaced with '-' */
			aname = t_strdup_noconst(vname);
			for (p = aname; *p != '\0'; p++) {
				if (*p == '_')
					*p = '-';
			}
			ext_value = ext_environment_item_get_value
				(ectx->env_ext, renv, aname);
		}

		if ( ext_value == NULL ) {
			*str_r = t_str_new_const("", 0);
			return SIEVE_EXEC_OK;
		}

		*str_r = t_str_new_const(ext_value, strlen(ext_value));
	}
	return SIEVE_EXEC_OK;
}

/*
 * Namespace registration
 */

static const struct sieve_extension_objects environment_namespaces =
	SIEVE_VARIABLES_DEFINE_NAMESPACE(environment_namespace);

const struct sieve_operand_def environment_namespace_operand = {
	.name = "env-namespace",
	.ext_def = &vnd_environment_extension,
	.class = &sieve_variables_namespace_operand_class,
	.interface = &environment_namespaces
};

void ext_environment_variables_init
(const struct sieve_extension *this_ext, struct sieve_validator *valdtr)
{
	struct ext_vnd_environment_context *ext_data =
		(struct ext_vnd_environment_context *) this_ext->context;

	sieve_variables_namespace_register
		(ext_data->var_ext, valdtr, this_ext, &environment_namespace);
}
