/* Extension copy
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 3894
 * Implementation: full
 * Status: experimental, largely untested
 * 
 */

#include <stdio.h>

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

/* Forward declarations */

static bool ext_copy_load(int ext_id);
static bool ext_copy_validator_load(struct sieve_validator *validator);

/* Extension definitions */

static const struct sieve_operand copy_side_effect_operand;

static int ext_my_id;

const struct sieve_extension copy_extension = { 
	"copy", 
	&ext_my_id,
	ext_copy_load,
	ext_copy_validator_load, 
	NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS,
	SIEVE_EXT_DEFINE_OPERAND(copy_side_effect_operand)
};

static bool ext_copy_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Side effect */

static void seff_copy_print
	(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
		const struct sieve_result_print_env *rpenv, void *se_context, bool *keep);
static bool seff_copy_post_commit
	(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
		const struct sieve_action_exec_env *aenv, void *se_context,
		void *tr_context, bool *keep);

const struct sieve_side_effect copy_side_effect = {
	SIEVE_OBJECT("copy", &copy_side_effect_operand, 0),
	&act_store,
	NULL, NULL,
	seff_copy_print,
	NULL, NULL,
	seff_copy_post_commit, 
	NULL
};

static const struct sieve_extension_obj_registry ext_side_effects =
    SIEVE_EXT_DEFINE_SIDE_EFFECT(copy_side_effect);

static const struct sieve_operand copy_side_effect_operand = {
    "copy operand",
    &copy_extension,
    0,
    &sieve_side_effect_operand_class,
    &ext_side_effects
};

/* Tag validation */

static bool tag_copy_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_ast_argument **arg ATTR_UNUSED, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	/* FIXME: currently not generated * /
	*arg = sieve_ast_arguments_detach(*arg,1);
	*/	
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/* Tag generation */

static bool tag_copy_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
    struct sieve_command_context *context ATTR_UNUSED)
{
    if ( sieve_ast_argument_type(arg) != SAAT_TAG ) {
        return FALSE;
    }

    sieve_opr_side_effect_emit(cgenv->sbin, &copy_side_effect);

    return TRUE;
}

/* Tag */

static const struct sieve_argument copy_tag = { 
	"copy", 
	NULL, NULL,
	tag_copy_validate, 
	NULL,
	tag_copy_generate
};

/* Side effect execution */

static void seff_copy_print
(const struct sieve_side_effect *seffect ATTR_UNUSED, 
	const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_result_print_env *rpenv,
	void *se_context ATTR_UNUSED, bool *keep)
{
	sieve_result_seffect_printf(rpenv, "preserve implicit keep");

	*keep = TRUE;
}

static bool seff_copy_post_commit
(const struct sieve_side_effect *seffect ATTR_UNUSED, 
	const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv ATTR_UNUSED, 
		void *se_context ATTR_UNUSED,	void *tr_context ATTR_UNUSED, bool *keep)
{	
	*keep = TRUE;
	return TRUE;
}

/* Load extension into validator */
static bool ext_copy_validator_load(struct sieve_validator *validator)
{
	/* Register copy tag with redirect and fileinto commands and we don't care
	 * whether these commands are registered or even whether they will be
	 * registered at all. The validator handles either situation gracefully 
	 */
	sieve_validator_register_external_tag(validator, &copy_tag, "redirect", -1);
	sieve_validator_register_external_tag(validator, &copy_tag, "fileinto", -1);

	return TRUE;
}



