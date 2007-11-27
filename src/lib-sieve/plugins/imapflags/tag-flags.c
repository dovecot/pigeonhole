#include "lib.h"

#include "sieve-commands.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"

static bool tag_flags_validate
	(struct sieve_validator *validator,	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd);

/* Tag */

const struct sieve_argument tag_flags = { 
	"flags", NULL, 
	tag_flags_validate, 
	NULL, NULL 
};

/* Tag validation */

static bool tag_flags_validate
(struct sieve_validator *validator,	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);
	
	/* Check syntax:
	 *   :flags <list-of-flags: string-list>
	 */
	if ( !sieve_validate_tag_parameter
		(validator, cmd, tag, *arg, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	/* Detach parameter */
	*arg = sieve_ast_arguments_detach(*arg,1);

	return TRUE;
}
