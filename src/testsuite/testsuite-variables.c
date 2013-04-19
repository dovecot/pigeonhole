/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

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

#include "testsuite-common.h"
#include "testsuite-variables.h"

/*
 *
 */

static const struct sieve_extension *testsuite_ext_variables = NULL;

/*
 *
 */

bool testsuite_varnamespace_validate
	(struct sieve_validator *valdtr, const struct sieve_variables_namespace *nspc,
		struct sieve_ast_argument *arg, struct sieve_command *cmd,
		ARRAY_TYPE(sieve_variable_name) *var_name, void **var_data,
		bool assignment);
bool testsuite_varnamespace_generate
	(const struct sieve_codegen_env *cgenv,
		const struct sieve_variables_namespace *nspc,
		struct sieve_ast_argument *arg, struct sieve_command *cmd, void *var_data);
bool testsuite_varnamespace_dump_variable
	(const struct sieve_dumptime_env *denv,
		const struct sieve_variables_namespace *nspc,
		const struct sieve_operand *oprnd, sieve_size_t *address);
int testsuite_varnamespace_read_variable
	(const struct sieve_runtime_env *renv,
		const struct sieve_variables_namespace *nspc,
		const struct sieve_operand *oprnd, sieve_size_t *address, string_t **str_r);

static const struct sieve_variables_namespace_def testsuite_namespace = {
	SIEVE_OBJECT("tst", &testsuite_namespace_operand, 0),
	testsuite_varnamespace_validate,
	testsuite_varnamespace_generate,
	testsuite_varnamespace_dump_variable,
	testsuite_varnamespace_read_variable
};

bool testsuite_varnamespace_validate
(struct sieve_validator *valdtr,
	const struct sieve_variables_namespace *nspc ATTR_UNUSED,
	struct sieve_ast_argument *arg, struct sieve_command *cmd ATTR_UNUSED,
	ARRAY_TYPE(sieve_variable_name) *var_name, void **var_data,
	bool assignment)
{
	struct sieve_ast *ast = arg->ast;
	const struct sieve_variable_name *name_element;
	const char *variable;

	/* Check variable name */

	if ( array_count(var_name) != 2 ) {
		sieve_argument_validate_error(valdtr, arg,
			"testsuite: invalid variable name within testsuite namespace: "
			"encountered sub-namespace");
		return FALSE;
 	}

	name_element = array_idx(var_name, 1);
	if ( name_element->num_variable >= 0 ) {
		sieve_argument_validate_error(valdtr, arg,
			"testsuite: invalid variable name within testsuite namespace 'tst.%d': "
			"encountered numeric variable name", name_element->num_variable);
		return FALSE;
	}

	variable = str_c(name_element->identifier);

	if ( assignment ) {
		sieve_argument_validate_error(valdtr, arg,
			"testsuite: cannot assign to testsuite variable 'tst.%s'", variable);
		return FALSE;
	}

	*var_data = (void *) p_strdup(sieve_ast_pool(ast), variable);

	return TRUE;
}

bool testsuite_varnamespace_generate
(const struct sieve_codegen_env *cgenv,
	const struct sieve_variables_namespace *nspc,
	struct sieve_ast_argument *arg ATTR_UNUSED,
	struct sieve_command *cmd ATTR_UNUSED, void *var_data)
{
	const struct sieve_extension *this_ext = SIEVE_OBJECT_EXTENSION(nspc);
	const char *variable = (const char *) var_data;

	if ( this_ext == NULL )
		return FALSE;

	sieve_variables_opr_namespace_variable_emit
		(cgenv->sblock, testsuite_ext_variables, this_ext, &testsuite_namespace);
	sieve_binary_emit_cstring(cgenv->sblock, variable);

	return TRUE;
}

bool testsuite_varnamespace_dump_variable
(const struct sieve_dumptime_env *denv,
	const struct sieve_variables_namespace *nspc ATTR_UNUSED,
	const struct sieve_operand *oprnd, sieve_size_t *address)
{
	string_t *var_name;

	if ( !sieve_binary_read_string(denv->sblock, address, &var_name) )
		return FALSE;

	if ( oprnd->field_name != NULL )
		sieve_code_dumpf(denv, "%s: VAR ${tst.%s}",
			oprnd->field_name, str_c(var_name));
	else
		sieve_code_dumpf(denv, "VAR ${tst.%s}",
			str_c(var_name));

	return TRUE;
}

int testsuite_varnamespace_read_variable
(const struct sieve_runtime_env *renv,
	const struct sieve_variables_namespace *nspc ATTR_UNUSED,
	const struct sieve_operand *oprnd, sieve_size_t *address,
	string_t **str_r)
{
	string_t *var_name;

	if ( !sieve_binary_read_string(renv->sblock, address, &var_name) ) {
		sieve_runtime_trace_operand_error(renv, oprnd,
			"testsuite variable operand corrupt: invalid name");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	if ( str_r != NULL ) {
		if ( strcmp(str_c(var_name), "path") == 0 )
			*str_r = t_str_new_const(testsuite_test_path, strlen(testsuite_test_path));
		else
			*str_r = NULL;
	}
	return SIEVE_EXEC_OK;
}


/*
 * Namespace registration
 */

static const struct sieve_extension_objects testsuite_namespaces =
	SIEVE_VARIABLES_DEFINE_NAMESPACE(testsuite_namespace);

const struct sieve_operand_def testsuite_namespace_operand = {
	"testsuite-namespace",
	&testsuite_extension,
	TESTSUITE_OPERAND_NAMESPACE,
	&sieve_variables_namespace_operand_class,
	&testsuite_namespaces
};

void testsuite_variables_init
(const struct sieve_extension *this_ext, struct sieve_validator *valdtr)
{
	testsuite_ext_variables = sieve_ext_variables_get_extension(this_ext->svinst);

	sieve_variables_namespace_register
		(testsuite_ext_variables, valdtr, this_ext, &testsuite_namespace);
}
