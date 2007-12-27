/* Extension imapflags
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-imapflags-05
 * Implementation: flag management works, not stored though. 
 * Status: under development
 *
 */
 
/* FIXME: As long as variables extension is not implemented, this extension will
 * not support the variable parameter to the commands/tests/tags.
 */

#include "lib.h"
#include "mempool.h"
#include "str.h"

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
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
static bool ext_imapflags_binary_load(struct sieve_binary *sbin);

/* Commands */

extern const struct sieve_command cmd_setflag;
extern const struct sieve_command cmd_addflag;
extern const struct sieve_command cmd_removeflag;

extern const struct sieve_command tst_hasflag;

/* Tagged arguments */

extern const struct sieve_argument tag_flags;

/* Operations */

extern const struct sieve_operation setflag_operation;
extern const struct sieve_operation addflag_operation;
extern const struct sieve_operation removeflag_operation;
extern const struct sieve_operation hasflag_operation;

const struct sieve_operation *imapflags_operations[] = 
	{ &setflag_operation, &addflag_operation, &removeflag_operation, &hasflag_operation };

/* Extension definitions */

int ext_imapflags_my_id;

const struct sieve_extension imapflags_extension = { 
	"imap4flags", 
	ext_imapflags_load,
	ext_imapflags_validator_load, 
	NULL, 
	ext_imapflags_binary_load,
	ext_imapflags_interpreter_load, 
	SIEVE_EXT_DEFINE_OPERATIONS(imapflags_operations), 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_imapflags_load(int ext_id)
{
	ext_imapflags_my_id = ext_id;

	return TRUE;
}

extern const struct sieve_side_effect_extension imapflags_seffect_extension;

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
 * Binary context
 */

static bool ext_imapflags_binary_load(struct sieve_binary *sbin)
{
	sieve_side_effect_extension_set(sbin, ext_imapflags_my_id, 
		&imapflags_seffect_extension);

	return TRUE;
}

/*
 * Interpreter context
 */

static bool ext_imapflags_interpreter_load
	(struct sieve_interpreter *interpreter ATTR_UNUSED)
{
	/* Will contain something when variables extension is implemented */
	return TRUE;
}



