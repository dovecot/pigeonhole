/* Extension imapflags
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-imapflags-05
 * Implementation: flag management works, not stored though. 
 * Status: under development
 *
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
static bool ext_imapflags_validator_load(struct sieve_validator *valdtr);
static bool ext_imapflags_runtime_load
	(const struct sieve_runtime_env *renv);
static bool ext_imapflags_binary_load(struct sieve_binary *sbin);

/* Commands */

extern const struct sieve_command cmd_setflag;
extern const struct sieve_command cmd_addflag;
extern const struct sieve_command cmd_removeflag;

extern const struct sieve_command tst_hasflag;

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
	ext_imapflags_runtime_load, 
	ext_imapflags_binary_load,
	NULL,
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
	(struct sieve_validator *valdtr)
{
	/* Register commands */
	sieve_validator_register_command(valdtr, &cmd_setflag);
	sieve_validator_register_command(valdtr, &cmd_addflag);
	sieve_validator_register_command(valdtr, &cmd_removeflag);
	sieve_validator_register_command(valdtr, &tst_hasflag);
	
	ext_imapflags_attach_flags_tag(valdtr, "keep");
	ext_imapflags_attach_flags_tag(valdtr, "fileinto");

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

static bool ext_imapflags_runtime_load
	(const struct sieve_runtime_env *renv)
{
	ext_imapflags_runtime_init(renv);
	
	return TRUE;
}



