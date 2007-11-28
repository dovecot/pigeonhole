/* Extension imapflags
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-imapflags-05
 * Implementation: flag managesiement works, not stored though. 
 * Status: under development
 *
 */

#include "lib.h"
#include "mempool.h"
#include "str.h"

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"


/* Forward declarations */

static bool ext_imapflags_load(int ext_id);
static bool ext_imapflags_validator_load(struct sieve_validator *validator);
static bool ext_imapflags_interpreter_load
	(struct sieve_interpreter *interpreter);

/* Commands */

extern const struct sieve_command cmd_setflag;
extern const struct sieve_command cmd_addflag;
extern const struct sieve_command cmd_removeflag;

extern const struct sieve_command tst_hasflag;

/* Tagged arguments */

extern const struct sieve_argument tag_flags;

/* Opcodes */

extern const struct sieve_opcode setflag_opcode;
extern const struct sieve_opcode addflag_opcode;
extern const struct sieve_opcode removeflag_opcode;
extern const struct sieve_opcode hasflag_opcode;

const struct sieve_opcode *imapflags_opcodes[] = 
	{ &setflag_opcode, &addflag_opcode, &removeflag_opcode, &hasflag_opcode };

/* Extension definitions */

int ext_imapflags_my_id;

const struct sieve_extension imapflags_extension = { 
	"imap4flags", 
	ext_imapflags_load,
	ext_imapflags_validator_load, 
	NULL, NULL,
	ext_imapflags_interpreter_load, 
	SIEVE_EXT_DEFINE_OPCODES(imapflags_opcodes), 
	NULL
};

static bool ext_imapflags_load(int ext_id)
{
	ext_imapflags_my_id = ext_id;

	return TRUE;
}

/* Load extension into validator */

static bool ext_imapflags_validator_load
	(struct sieve_validator *validator)
{
	/* Register commands */
	sieve_validator_register_command(validator, &cmd_setflag);
	sieve_validator_register_command(validator, &cmd_addflag);
	sieve_validator_register_command(validator, &cmd_removeflag);
	sieve_validator_register_command(validator, &tst_hasflag);
	
	/* Register :flags tag with keep and fileinto commands and we don't care
	 * whether these commands are registered or even whether they will be
	 * registered at all. The validator handles either situation gracefully 
	 */
	sieve_validator_register_external_tag(validator, &tag_flags, "keep", -1);
	sieve_validator_register_external_tag(validator, &tag_flags, "fileinto", -1);

	return TRUE;
}

/*
 * Interpreter context
 */

static bool ext_imapflags_interpreter_load
	(struct sieve_interpreter *interpreter)
{
	pool_t pool = sieve_interpreter_pool(interpreter);
	
	struct ext_imapflags_interpreter_context *ctx = 
		p_new(pool, struct ext_imapflags_interpreter_context, 1);
	
	ctx->internal_flags = str_new(pool, 32);
	
	sieve_interpreter_extension_set_context
		(interpreter, ext_imapflags_my_id, ctx);
	
	return TRUE;
}



