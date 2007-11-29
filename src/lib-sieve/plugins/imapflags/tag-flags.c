#include "lib.h"
#include "array.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-result.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"

static bool tag_flags_validate
	(struct sieve_validator *validator,	struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);
static bool tag_flags_generate
	(struct sieve_generator *generator, struct sieve_ast_argument *arg,
		struct sieve_command_context *cmd);

/* Tag */

const struct sieve_argument tag_flags = { 
	"flags", NULL, 
	tag_flags_validate, 
	NULL, 
	tag_flags_generate 
};

/* Side effect */

const struct sieve_side_effect_extension ext_flags_side_effect;

bool seff_flags_read
	(const struct sieve_side_effect *seffect, 
		const struct sieve_runtime_env *renv, sieve_size_t *address,
		void **se_context);
void seff_flags_print
	(const struct sieve_side_effect *seffect,	const struct sieve_action *action, 
		void *se_context, bool *keep);
bool seff_flags_pre_execute
	(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
		const struct sieve_action_exec_env *aenv, void **se_context, 
		void *tr_context);
bool seff_flags_post_commit
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *se_context,
			void *tr_context, bool *keep);

const struct sieve_side_effect flags_side_effect = {
	"flags",
	&act_store,
	
	&ext_flags_side_effect,
	0,
	seff_flags_read,
	seff_flags_print,
	NULL, NULL,
	seff_flags_post_commit, 
	NULL
};

const struct sieve_side_effect_extension imapflags_seffect_extension = {
	&imapflags_extension,

	SIEVE_EXT_DEFINE_SIDE_EFFECT(flags_side_effect)
};

/* Tag validation */

static bool tag_flags_validate
(struct sieve_validator *validator,	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check syntax:
	 *   :flags <list-of-flags: string-list>
	 */
	if ( !sieve_validate_tag_parameter
		(validator, cmd, tag, *arg, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	tag->parameters = *arg;
	
	/* Detach parameter */
	*arg = sieve_ast_arguments_detach(*arg,1);

	return TRUE;
}

/* Tag generation */

static bool tag_flags_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg,
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *param;
  struct sieve_binary *sbin = sieve_generator_get_binary(generator);

  if ( sieve_ast_argument_type(arg) != SAAT_TAG ) {
      return FALSE;
  }

  sieve_opr_side_effect_emit(sbin, &flags_side_effect, ext_imapflags_my_id);

	param = arg->parameters;
	
	/* Call the generation function for the argument */ 
	if ( param->argument != NULL && param->argument->generate != NULL && 
		!param->argument->generate(generator, param, cmd) ) 
		return FALSE;
	
  return TRUE;
}

/* Side effect execution */

bool seff_flags_read
(const struct sieve_side_effect *seffect ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address,
	void **se_context)
{
	pool_t pool = sieve_result_pool(renv->result);
	ARRAY_DEFINE(flags, const char *);
	string_t *flag_item;
	struct sieve_coded_stringlist *flag_list;
	
	p_array_init(&flags, pool, 2);
	
	t_push();
	
	/* Read key-list */
	if ( (flag_list=sieve_opr_stringlist_read(renv->sbin, address)) == NULL ) {
		t_pop();
		return FALSE;
	}
	
	/* Iterate through all requested headers to match */
	flag_item = NULL;
	while ( sieve_coded_stringlist_next_item(flag_list, &flag_item) && 
		flag_item != NULL ) {
		const char *flag = str_c(flag_item);
		
		array_append(&flags, &flag, 1);
	}
	
	(void)array_append_space(&flags);
	*se_context = (void **) array_idx(&flags, 0);

	t_pop();
	
	return TRUE;
}

void seff_flags_print
(const struct sieve_side_effect *seffect ATTR_UNUSED, 
	const struct sieve_action *action ATTR_UNUSED, 
	void *se_context ATTR_UNUSED, bool *keep)
{
	const char *const *flags = (const char *const *) se_context;
	
	printf("        + add flags:");

	while ( *flags != NULL ) {
		printf(" %s", *flags);
		
		flags++;
	};
	
	printf("\n");

	*keep = TRUE;
}

bool seff_flags_post_commit
(const struct sieve_side_effect *seffect ATTR_UNUSED, 
	const struct sieve_action *action, 
	const struct sieve_action_exec_env *aenv ATTR_UNUSED, 
	void *se_context ATTR_UNUSED,	void *tr_context ATTR_UNUSED, bool *keep)
{	
	i_info("implicit keep preserved after %s action.", action->name);
	*keep = TRUE;
	return TRUE;
}


