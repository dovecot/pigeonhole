/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mail-storage.h"
#include "mail-namespace.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-result.h"
#include "sieve-generator.h"

#include "ext-mailbox-common.h"

/*
 * Tagged argument
 */

static bool
tag_mailbox_create_validate(struct sieve_validator *valdtr,
			    struct sieve_ast_argument **arg,
			    struct sieve_command *cmd);
static bool
tag_mailbox_create_generate(const struct sieve_codegen_env *cgenv,
			    struct sieve_ast_argument *arg,
			    struct sieve_command *context);

const struct sieve_argument_def mailbox_create_tag = {
	.identifier = "create",
	.validate = tag_mailbox_create_validate,
	.generate = tag_mailbox_create_generate
};

/*
 * Side effect
 */

static void
seff_mailbox_create_print(const struct sieve_side_effect *seffect,
			  const struct sieve_action *action,
			  const struct sieve_result_print_env *rpenv,
			  bool *keep);
static int
seff_mailbox_create_pre_execute(const struct sieve_side_effect *seffect,
				const struct sieve_action_exec_env *aenv,
				void **se_context, void *tr_context);

const struct sieve_side_effect_def mailbox_create_side_effect = {
	SIEVE_OBJECT("create", &mailbox_create_operand, 0),
	.precedence = 100,
	.to_action = &act_store,
	.print = seff_mailbox_create_print,
	.pre_execute = seff_mailbox_create_pre_execute
};

/*
 * Operand
 */

static const struct sieve_extension_objects ext_side_effects =
	SIEVE_EXT_DEFINE_SIDE_EFFECT(mailbox_create_side_effect);

const struct sieve_operand_def mailbox_create_operand = {
	.name = "create operand",
	.ext_def = &mailbox_extension,
	.class = &sieve_side_effect_operand_class,
	.interface = &ext_side_effects
};

/*
 * Tag validation
 */

static bool
tag_mailbox_create_validate(struct sieve_validator *valdtr ATTR_UNUSED,
			    struct sieve_ast_argument **arg,
			    struct sieve_command *cmd ATTR_UNUSED)
{
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/*
 * Code generation
 */

static bool
tag_mailbox_create_generate(const struct sieve_codegen_env *cgenv,
			    struct sieve_ast_argument *arg,
			    struct sieve_command *context ATTR_UNUSED)
{
	if (sieve_ast_argument_type(arg) != SAAT_TAG)
		return FALSE;

	sieve_opr_side_effect_emit(cgenv->sblock, arg->argument->ext,
				   &mailbox_create_side_effect);
	return TRUE;
}

/*
 * Side effect implementation
 */

static void
seff_mailbox_create_print(const struct sieve_side_effect *seffect ATTR_UNUSED,
			  const struct sieve_action *action ATTR_UNUSED,
			  const struct sieve_result_print_env *rpenv,
			  bool *keep ATTR_UNUSED)
{
	sieve_result_seffect_printf(
		rpenv, "create mailbox if it does not exist");
}

static int
seff_mailbox_create_pre_execute(
	const struct sieve_side_effect *seffect ATTR_UNUSED,
	const struct sieve_action_exec_env *aenv,
	void **se_context ATTR_UNUSED, void *tr_context ATTR_UNUSED)
{
	struct act_store_transaction *trans =
		(struct act_store_transaction *)tr_context;
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct mailbox *box = trans->box;

	/* Check whether creation is necessary */
	if (box == NULL || trans->disabled)
		return SIEVE_EXEC_OK;

	eenv->exec_status->last_storage = mailbox_get_storage(box);

	/* Open the mailbox (may already be open) */
	if (trans->error_code == MAIL_ERROR_NONE) {
		if (mailbox_open(box) < 0)
			sieve_act_store_get_storage_error(aenv, trans);
	}

	/* Check whether creation has a chance of working */
	switch (trans->error_code) {
	case MAIL_ERROR_NONE:
		return SIEVE_EXEC_OK;
	case MAIL_ERROR_NOTFOUND:
		break;
	case MAIL_ERROR_TEMP:
		return SIEVE_EXEC_TEMP_FAILURE;
	default:
		return SIEVE_EXEC_FAILURE;
	}

	trans->error = NULL;
	trans->error_code = MAIL_ERROR_NONE;

	/* Create mailbox */
	if (mailbox_create(box, NULL, FALSE) < 0) {
		sieve_act_store_get_storage_error(aenv, trans);
		if (trans->error_code == MAIL_ERROR_EXISTS) {
			trans->error = NULL;
			trans->error_code = MAIL_ERROR_NONE;
		} else {
			return (trans->error_code == MAIL_ERROR_TEMP ?
				SIEVE_EXEC_TEMP_FAILURE : SIEVE_EXEC_FAILURE);
		}
	}

	/* Subscribe to it if necessary */
	if (eenv->scriptenv->mailbox_autosubscribe) {
		(void)mailbox_list_set_subscribed(
			mailbox_get_namespace(box)->list,
			mailbox_get_name(box), TRUE);
	}

	/* Try opening again */
	if (mailbox_open(box) < 0) {
		/* Failed definitively */
		sieve_act_store_get_storage_error(aenv, trans);
		return (trans->error_code == MAIL_ERROR_TEMP ?
			SIEVE_EXEC_TEMP_FAILURE : SIEVE_EXEC_FAILURE);
	}
	return SIEVE_EXEC_OK;
}



