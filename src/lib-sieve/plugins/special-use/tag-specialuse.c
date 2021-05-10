/* Copyright (c) 2019 Pigeonhole authors, see the included COPYING file */

#include "lib.h"
#include "str-sanitize.h"
#include "mail-storage.h"
#include "mail-namespace.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-result.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-special-use-common.h"

/*
 * Flags tagged argument
 */

static bool
tag_specialuse_validate(struct sieve_validator *valdtr,
			struct sieve_ast_argument **arg,
			struct sieve_command *cmd);
static bool
tag_specialuse_generate(const struct sieve_codegen_env *cgenv,
			struct sieve_ast_argument *arg,
			struct sieve_command *cmd);

const struct sieve_argument_def specialuse_tag = {
	.identifier = "specialuse",
	.validate = tag_specialuse_validate,
	.generate = tag_specialuse_generate
};

/*
 * Side effect
 */

static bool
seff_specialuse_dump_context(const struct sieve_side_effect *seffect,
			     const struct sieve_dumptime_env *denv,
			     sieve_size_t *address);
static int
seff_specialuse_read_context(const struct sieve_side_effect *seffect,
			     const struct sieve_runtime_env *renv,
			     sieve_size_t *address, void **context);

static int
seff_specialuse_merge(const struct sieve_runtime_env *renv,
		      const struct sieve_action *action,
		      const struct sieve_side_effect *old_seffect,
		      const struct sieve_side_effect *new_seffect,
		      void **old_context);

static void
seff_specialuse_print(const struct sieve_side_effect *seffect,
		      const struct sieve_action *action,
		      const struct sieve_result_print_env *rpenv, bool *keep);

static int
seff_specialuse_pre_execute(const struct sieve_side_effect *seffect,
			    const struct sieve_action_exec_env *aenv,
			    void *tr_context, void **se_tr_context ATTR_UNUSED);

const struct sieve_side_effect_def specialuse_side_effect = {
	SIEVE_OBJECT("specialuse", &specialuse_operand, 0),
	.precedence = 200,
	.to_action = &act_store,
	.dump_context = seff_specialuse_dump_context,
	.read_context = seff_specialuse_read_context,
	.merge = seff_specialuse_merge,
	.print = seff_specialuse_print,
	.pre_execute = seff_specialuse_pre_execute
};

/*
 * Operand
 */

static const struct sieve_extension_objects ext_side_effects =
	SIEVE_EXT_DEFINE_SIDE_EFFECT(specialuse_side_effect);

const struct sieve_operand_def specialuse_operand = {
	.name = "specialuse operand",
	.ext_def = &special_use_extension,
	.class = &sieve_side_effect_operand_class,
	.interface = &ext_side_effects
};

/*
 * Tag validation
 */

static bool
tag_specialuse_validate(struct sieve_validator *valdtr,
			struct sieve_ast_argument **arg,
			struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_argument_next(*arg);

	/* Check syntax:
	 *   :specialuse <special-use-flag: string>
	 */
	if (!sieve_validate_tag_parameter(valdtr, cmd, tag, *arg, NULL, 0,
					  SAAT_STRING, FALSE))
		return FALSE;
	if (sieve_argument_is_string_literal(*arg)) {
		const char *use_flag = sieve_ast_argument_strc(*arg);

		if (!ext_special_use_flag_valid(use_flag)) {
			sieve_argument_validate_error(
				valdtr, *arg, "specialuse tag: "
				"invalid special-use flag `%s' specified",
				str_sanitize(use_flag, 64));
			return FALSE;
		}
	}

	tag->parameters = *arg;

	/* Detach parameter */
	*arg = sieve_ast_arguments_detach(*arg,1);

	return TRUE;
}

/*
 * Code generation
 */

static bool
tag_specialuse_generate(const struct sieve_codegen_env *cgenv,
			struct sieve_ast_argument *arg,
			struct sieve_command *cmd)
{
	struct sieve_ast_argument *param;

	if (sieve_ast_argument_type(arg) != SAAT_TAG)
		return FALSE;

	sieve_opr_side_effect_emit(cgenv->sblock, arg->argument->ext,
				   &specialuse_side_effect);

	/* Explicit :specialuse tag */
	param = arg->parameters;

	/* Call the generation function for the argument */
	if (param->argument != NULL && param->argument->def != NULL &&
	    param->argument->def->generate != NULL &&
	    !param->argument->def->generate(cgenv, param, cmd))
		return FALSE;

	return TRUE;
}

/*
 * Side effect implementation
 */

/* Context data */

struct seff_specialuse_context {
	const char *special_use_flag;
};

/* Context coding */

static bool
seff_specialuse_dump_context(
	const struct sieve_side_effect *seffect ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	return sieve_opr_stringlist_dump(denv, address, "specialuse");
}

static int
seff_specialuse_read_context(
	const struct sieve_side_effect *seffect ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address,
	void **se_context)
{
	pool_t pool = sieve_result_pool(renv->result);
	struct seff_specialuse_context *ctx;
	string_t *special_use_flag;
	const char *use_flag;
	int ret;

	if ((ret = sieve_opr_string_read(renv, address, "specialuse",
					 &special_use_flag)) <= 0)
		return ret;

	use_flag = str_c(special_use_flag);
	if (!ext_special_use_flag_valid(use_flag)) {
		sieve_runtime_error(
			renv, NULL, "specialuse tag: "
			"invalid special-use flag `%s' specified",
			str_sanitize(use_flag, 64));
		return SIEVE_EXEC_FAILURE;
	}

	ctx = p_new(pool, struct seff_specialuse_context, 1);
	ctx->special_use_flag = p_strdup(pool, use_flag);

	*se_context = (void *) ctx;

	return SIEVE_EXEC_OK;
}

/* Result verification */

static int
seff_specialuse_merge(const struct sieve_runtime_env *renv ATTR_UNUSED,
		      const struct sieve_action *action ATTR_UNUSED,
		      const struct sieve_side_effect *old_seffect ATTR_UNUSED,
		      const struct sieve_side_effect *new_seffect,
		      void **old_context)
{
	if (new_seffect != NULL)
		*old_context = new_seffect->context;

	return 1;
}

/* Result printing */

static void
seff_specialuse_print(const struct sieve_side_effect *seffect,
		      const struct sieve_action *action ATTR_UNUSED,
		      const struct sieve_result_print_env *rpenv,
		      bool *keep ATTR_UNUSED)
{
	struct seff_specialuse_context *ctx =
		(struct seff_specialuse_context *)seffect->context;

	sieve_result_seffect_printf(
		rpenv,
		"use mailbox with special-use flag `%s' instead if accessible",
		ctx->special_use_flag);
}

/* Result execution */

static int
seff_specialuse_pre_execute(const struct sieve_side_effect *seffect,
			    const struct sieve_action_exec_env *aenv,
			    void *tr_context, void **se_tr_context ATTR_UNUSED)
{
	struct seff_specialuse_context *ctx =
		(struct seff_specialuse_context *)seffect->context;
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct act_store_transaction *trans =
		(struct act_store_transaction *)tr_context;
	struct mailbox *box;

	if (trans->box == NULL || trans->disabled)
		return SIEVE_EXEC_OK;

	/* Check whether something already failed */
	switch (trans->error_code) {
	case MAIL_ERROR_NONE:
		break;
	case MAIL_ERROR_TEMP:
		return SIEVE_EXEC_TEMP_FAILURE;
	default:
		return SIEVE_EXEC_FAILURE;
	}

	trans->error = NULL;
	trans->error_code = MAIL_ERROR_NONE;

	box = mailbox_alloc_for_user(eenv->scriptenv->user,
				     ctx->special_use_flag,
				     (MAILBOX_FLAG_POST_SESSION |
				      MAILBOX_FLAG_SPECIAL_USE));

	/* We still override the allocate default mailbox with ours below even
	   when the default and special-use mailbox are identical. Choosing
	   either one is (currently) equal and setting trans->mailbox_identifier
	   for SPECIAL-USE needs to be done either way, so we use the same code
	   path. */

	/* Try to open the mailbox */
	eenv->exec_status->last_storage = mailbox_get_storage(box);
	if (mailbox_open(box) == 0) {
		pool_t pool = sieve_result_pool(aenv->result);

		/* Success */
		mailbox_free(&trans->box);
		trans->box = box;
		trans->mailbox_identifier = p_strdup_printf(pool,
			"[SPECIAL-USE %s]", ctx->special_use_flag);

	} else {
		/* Failure */
		if (mailbox_get_last_mail_error(box) == MAIL_ERROR_NOTFOUND) {
			/* Not found; revert to default */
			mailbox_free(&box);
		} else {
			/* Total failure */
			mailbox_free(&trans->box);
			trans->box = box;
			sieve_act_store_get_storage_error(aenv, trans);
			return (trans->error_code == MAIL_ERROR_TEMP ?
				SIEVE_EXEC_TEMP_FAILURE : SIEVE_EXEC_FAILURE);
		}
	}

	return SIEVE_EXEC_OK;
}
