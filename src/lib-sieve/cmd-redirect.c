#include "lib.h"
#include "str-sanitize.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-actions.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

/* Forward declarations */

static bool cmd_redirect_opcode_dump
	(const struct sieve_opcode *opcode,
		const struct sieve_runtime_env *renv, sieve_size_t *address);
static bool cmd_redirect_opcode_execute
	(const struct sieve_opcode *opcode, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_redirect_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_redirect_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Redirect command 
 * 
 * Syntax
 *   redirect <address: string>
 */

const struct sieve_command cmd_redirect = { 
	"redirect", 
	SCT_COMMAND,
	1, 0, FALSE, FALSE, 
	NULL, NULL,
	cmd_redirect_validate, 
	cmd_redirect_generate, 
	NULL 
};

/* Redirect opcode */

const struct sieve_opcode cmd_redirect_opcode = { 
	"REDIRECT",
	SIEVE_OPCODE_REDIRECT,
	NULL, 0,
	cmd_redirect_opcode_dump, 
	cmd_redirect_opcode_execute 
};

/* Redirect action */

static bool act_redirect_check_duplicate
	(const struct sieve_runtime_env *renv,
		const struct sieve_action *action1, void *context1, void *context2);
static void act_redirect_print
	(const struct sieve_action *action, void *context);	
static int act_redirect_execute
	(const struct sieve_action *action,	const struct sieve_action_exec_env *aenv, 
		void *context);
		
struct act_redirect_context {
	const char *to_address;
};

const struct sieve_action act_redirect = {
	"redirect",
	act_redirect_check_duplicate, 
	NULL,
	act_redirect_print,
	act_redirect_execute
};

/* Validation */

static bool cmd_redirect_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	/* Check argument */
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "address", 1, SAAT_STRING) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, arg);
	 
	return TRUE;
}

/*
 * Generation
 */
 
static bool cmd_redirect_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_opcode(generator, &cmd_redirect_opcode);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
	
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_redirect_opcode_dump
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	printf("REDIRECT\n");

	return 
		sieve_opr_string_dump(renv->sbin, address);
}

/*
 * Intepretation
 */

static bool cmd_redirect_opcode_execute
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct act_redirect_context *act;
	string_t *redirect;
	pool_t pool;

	t_push();

	if ( !sieve_opr_string_read(renv->sbin, address, &redirect) ) {
		t_pop();
		return FALSE;
	}

	printf(">> REDIRECT \"%s\"\n", str_c(redirect));
	
	/* Add redirect action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_redirect_context, 1);
	act->to_address = p_strdup(pool, str_c(redirect));
	
	sieve_result_add_action(renv, &act_redirect, (void *) act);
	
	t_pop();
	return TRUE;
}

/*
 * Action
 */
 
static bool act_redirect_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action *action1 ATTR_UNUSED, 
	void *context1, void *context2)
{
	struct act_redirect_context *ctx1 = (struct act_redirect_context *) context1;
	struct act_redirect_context *ctx2 = (struct act_redirect_context *) context2;
	
	if ( strcmp(ctx1->to_address, ctx2->to_address) == 0 ) 
		return TRUE;
		
	return FALSE;
}

static void act_redirect_print
(const struct sieve_action *action ATTR_UNUSED, void *context)	
{
	struct act_redirect_context *ctx = (struct act_redirect_context *) context;
	
	printf("* redirect message to: %s\n", ctx->to_address);
}

static int act_redirect_execute
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *context)
{
	const struct sieve_message_data *msgdata = aenv->msgdata;
	struct act_redirect_context *ctx = (struct act_redirect_context *) context;
	int res;
	
	if ((res = aenv->mailenv->
		send_forward(msgdata, ctx->to_address)) == 0) {
		i_info("msgid=%s: forwarded to <%s>",
			msgdata->id == NULL ? "" : str_sanitize(msgdata->id, 80),
				str_sanitize(ctx->to_address, 80));
  }
  
	return res;
}


