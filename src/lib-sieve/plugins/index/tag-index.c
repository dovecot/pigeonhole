/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mail-storage.h"
#include "mail-namespace.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-binary.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-result.h"
#include "sieve-validator.h"
#include "sieve-generator.h"

#include "ext-index-common.h"

/*
 * Tagged argument
 */

static bool tag_index_validate
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);
static bool tag_index_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
    struct sieve_command *context);

const struct sieve_argument_def index_tag = {
	.identifier = "index",
	.validate = tag_index_validate,
	.generate = tag_index_generate
};

static bool tag_last_validate
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);

const struct sieve_argument_def last_tag = {
	.identifier = "last",
	.validate = tag_last_validate,
};

/*
 * Header override
 */

static bool svmo_index_dump_context
	(const struct sieve_message_override *svmo,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int svmo_index_read_context
	(const struct sieve_message_override *svmo,
		const struct sieve_runtime_env *renv, sieve_size_t *address,
		void **ho_context);
static int svmo_index_header_override
	(const struct sieve_message_override *svmo,
		const struct sieve_runtime_env *renv,
		bool mime_decode, struct sieve_stringlist **headers);

const struct sieve_message_override_def index_header_override = {
	SIEVE_OBJECT("index", &index_operand, 0),
	.sequence = SIEVE_EXT_INDEX_HDR_OVERRIDE_SEQUENCE,
	.dump_context = svmo_index_dump_context,
	.read_context = svmo_index_read_context,
	.header_override = svmo_index_header_override
};

/*
 * Operand
 */

static const struct sieve_extension_objects ext_header_overrides =
	SIEVE_EXT_DEFINE_MESSAGE_OVERRIDE(index_header_override);

const struct sieve_operand_def index_operand = {
	.name = "index operand",
	.ext_def = &index_extension,
	.class = &sieve_message_override_operand_class,
	.interface = &ext_header_overrides
};

/*
 * Tag data
 */

struct tag_index_data {
	sieve_number_t fieldno;
	unsigned int last:1;
};

/*
 * Tag validation
 */

static bool tag_index_validate
(struct sieve_validator *valdtr ATTR_UNUSED,
	struct sieve_ast_argument **arg, struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct tag_index_data *data;

	/* Skip the tag itself */
	*arg = sieve_ast_argument_next(*arg);

	/* Check syntax:
	 *   ":index" <fieldno: number>
	 */
	if ( !sieve_validate_tag_parameter
		(valdtr, cmd, tag, *arg, NULL, 0, SAAT_NUMBER, FALSE) ) {
		return FALSE;
	}

	if (tag->argument->data == NULL) {
		data = p_new(sieve_command_pool(cmd), struct tag_index_data, 1);
		tag->argument->data = (void *)data;
	} else {
		data = (struct tag_index_data *)tag->argument->data;
	}

	data->fieldno = sieve_ast_argument_number(*arg);

	/* Detach parameter */
	*arg = sieve_ast_arguments_detach(*arg,1);
	return TRUE;
}

static bool tag_last_validate
(struct sieve_validator *valdtr ATTR_UNUSED,
	struct sieve_ast_argument **arg, struct sieve_command *cmd)
{
	struct sieve_ast_argument *index_arg;
	struct tag_index_data *data;

	index_arg = sieve_command_find_argument(cmd, &index_tag);
	if (index_arg == NULL) {
		sieve_argument_validate_error(valdtr, *arg,
			"the :last tag for the %s %s cannot be specified "
			"without the :index tag",
			sieve_command_identifier(cmd), sieve_command_type_name(cmd));
		return FALSE;
	}

	/* Set :last flag */
	if (index_arg->argument->data == NULL) {
		data = p_new(sieve_command_pool(cmd), struct tag_index_data, 1);
		index_arg->argument->data = (void*)data;
	} else {
		data = (struct tag_index_data *)index_arg->argument->data;
	}
	data->last = TRUE;

	/* Detach */
	*arg = sieve_ast_arguments_detach(*arg,1);
	return TRUE;
}

/*
 * Code generation
 */

static bool tag_index_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
	struct sieve_command *cmd ATTR_UNUSED)
{
	struct tag_index_data *data =
		(struct tag_index_data *)arg->argument->data;

	if ( sieve_ast_argument_type(arg) != SAAT_TAG )
		return FALSE;

	sieve_opr_message_override_emit
		(cgenv->sblock, arg->argument->ext, &index_header_override);

	(void)sieve_binary_emit_integer
		(cgenv->sblock, data->fieldno);
	(void)sieve_binary_emit_byte
		(cgenv->sblock, ( data->last ? 1 : 0 ));

	return TRUE;
}

/*
 * Header override implementation
 */

/* Context data */

struct svmo_index_context {
	unsigned int fieldno;
	unsigned int last:1;
};

/* Context coding */

static bool svmo_index_dump_context
(const struct sieve_message_override *svmo ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_number_t fieldno = 0;
	unsigned int last;

	if ( !sieve_binary_read_integer(denv->sblock, address, &fieldno) )
		return FALSE;

	sieve_code_dumpf(denv, "fieldno: %llu",
		(unsigned long long) fieldno);

	if ( !sieve_binary_read_byte(denv->sblock, address, &last) )
		return FALSE;

	if (last > 0)
		sieve_code_dumpf(denv, "last");
	return TRUE;
}

static int svmo_index_read_context
(const struct sieve_message_override *svmo ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address,
	void **ho_context)
{
	pool_t pool = sieve_result_pool(renv->result);
	struct svmo_index_context *ctx;
	sieve_number_t fieldno;
	unsigned int last = 0;

	if ( !sieve_binary_read_integer(renv->sblock, address, &fieldno) ) {
		sieve_runtime_trace_error(renv, "fieldno: invalid number");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	if ( !sieve_binary_read_byte(renv->sblock, address, &last) ) {
		sieve_runtime_trace_error(renv, "last: invalid byte");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	ctx = p_new(pool, struct svmo_index_context, 1);
	ctx->fieldno = fieldno;
	ctx->last = (last == 0 ? FALSE : TRUE);

	*ho_context = (void *) ctx;

	return SIEVE_EXEC_OK;
}

/* Override */

static int svmo_index_header_override
(const struct sieve_message_override *svmo,
	const struct sieve_runtime_env *renv,
	bool mime_decode ATTR_UNUSED,
	struct sieve_stringlist **headers)
{
	struct svmo_index_context *ctx =
		(struct svmo_index_context *)svmo->context;

	sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
		"header index override: only returning index %d%s",
		ctx->fieldno, ( ctx->last ? " (from last)" : "" ));

	*headers = sieve_index_stringlist_create(renv, *headers,
		(int)ctx->fieldno * ( ctx->last ? -1 : 1 ));
	return SIEVE_EXEC_OK;
}

