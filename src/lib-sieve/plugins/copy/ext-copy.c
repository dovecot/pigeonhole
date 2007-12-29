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
static bool ext_copy_binary_load(struct sieve_binary *sbin);

/* Extension definitions */

static int ext_my_id;

const struct sieve_extension copy_extension = { 
	"copy", 
	ext_copy_load,
	ext_copy_validator_load, 
	NULL, 
	ext_copy_binary_load, 
	NULL, 
	SIEVE_EXT_DEFINE_NO_OPERATIONS,
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_copy_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Side effect */

const struct sieve_side_effect_extension ext_copy_side_effect;

static void seff_copy_print
	(const struct sieve_side_effect *seffect,	const struct sieve_action *action, 
		void *se_context, bool *keep);
static bool seff_copy_post_commit
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *se_context,
			void *tr_context, bool *keep);

const struct sieve_side_effect copy_side_effect = {
	"copy",
	&act_store,	
	&ext_copy_side_effect,
	0,
	NULL, NULL,
	seff_copy_print,
	NULL, NULL,
	seff_copy_post_commit, 
	NULL
};

const struct sieve_side_effect_extension ext_copy_side_effect = {
	&copy_extension,

	SIEVE_EXT_DEFINE_SIDE_EFFECT(copy_side_effect)
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
(struct sieve_generator *generator, struct sieve_ast_argument *arg,
    struct sieve_command_context *context ATTR_UNUSED)
{
    struct sieve_binary *sbin = sieve_generator_get_binary(generator);

    if ( sieve_ast_argument_type(arg) != SAAT_TAG ) {
        return FALSE;
    }

    sieve_opr_side_effect_emit(sbin, &copy_side_effect, ext_my_id);

    return TRUE;
}

/* Tag */

static const struct sieve_argument copy_tag = { 
	"copy", NULL, 
	tag_copy_validate, 
	NULL,
	tag_copy_generate
};

/* Side effect execution */

static void seff_copy_print
	(const struct sieve_side_effect *seffect ATTR_UNUSED, 
		const struct sieve_action *action ATTR_UNUSED, 
		void *se_context ATTR_UNUSED, bool *keep)
{
	printf("        + preserve implicit keep\n");

	*keep = TRUE;
}

static bool seff_copy_post_commit
(const struct sieve_side_effect *seffect ATTR_UNUSED, 
	const struct sieve_action *action, 
	const struct sieve_action_exec_env *aenv ATTR_UNUSED, 
		void *se_context ATTR_UNUSED,	void *tr_context ATTR_UNUSED, bool *keep)
{	
	sieve_result_log(aenv, "implicit keep preserved after %s action.", action->name);
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

static bool ext_copy_binary_load(struct sieve_binary *sbin)
{
	sieve_side_effect_extension_set(sbin, ext_my_id, &ext_copy_side_effect);

	return TRUE;
}


