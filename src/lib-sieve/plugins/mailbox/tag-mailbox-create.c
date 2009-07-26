/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-result.h"
#include "sieve-generator.h"

#include "ext-mailbox-common.h"

/* 
 * Tagged argument 
 */

static bool tag_mailbox_create_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);
static bool tag_mailbox_create_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
    struct sieve_command_context *context);

const struct sieve_argument mailbox_create_tag = { 
	"create", 
	NULL, NULL,
	tag_mailbox_create_validate, 
	NULL,
	tag_mailbox_create_generate
};

/*
 * Side effect 
 */

static void seff_mailbox_create_print
	(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
		const struct sieve_result_print_env *rpenv, void *se_context, bool *keep);
static void seff_mailbox_create_post_commit
	(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
		const struct sieve_action_exec_env *aenv, void *se_context,
		void *tr_context, bool *keep);

const struct sieve_side_effect mailbox_create_side_effect = {
	SIEVE_OBJECT("create", &mailbox_create_operand, 0),
	&act_store,
	NULL, NULL, NULL,
	seff_mailbox_create_print,
	NULL, NULL,
	seff_mailbox_create_post_commit, 
	NULL
};

/*
 * Operand
 */

static const struct sieve_extension_objects ext_side_effects =
	SIEVE_EXT_DEFINE_SIDE_EFFECT(mailbox_create_side_effect);

const struct sieve_operand mailbox_create_operand = {
	"create operand",
	&mailbox_extension,
	0,
	&sieve_side_effect_operand_class,
	&ext_side_effects
};

/* 
 * Tag validation 
 */

static bool tag_mailbox_create_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_ast_argument **arg ATTR_UNUSED, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/*
 * Code generation 
 */

static bool tag_mailbox_create_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
    struct sieve_command_context *context ATTR_UNUSED)
{
	if ( sieve_ast_argument_type(arg) != SAAT_TAG ) {
		return FALSE;
	}

	sieve_opr_side_effect_emit(cgenv->sbin, &mailbox_create_side_effect);

	return TRUE;
}

/* 
 * Side effect implementation
 */

static void seff_mailbox_create_print
(const struct sieve_side_effect *seffect ATTR_UNUSED, 
	const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_result_print_env *rpenv,
	void *se_context ATTR_UNUSED, bool *keep ATTR_UNUSED)
{
	sieve_result_seffect_printf(rpenv, "create mailbox if it does not exist");
}

static void seff_mailbox_create_post_commit
(const struct sieve_side_effect *seffect ATTR_UNUSED, 
	const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv ATTR_UNUSED,
	void *se_context ATTR_UNUSED, void *tr_context ATTR_UNUSED, 
	bool *keep ATTR_UNUSED)
{	
}



