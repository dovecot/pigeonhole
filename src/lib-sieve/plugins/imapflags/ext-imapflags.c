/* Extension imapflags
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-imapflags-05
 * Implementation: skeleton
 * Status: under development
 *
 */

#include <stdio.h>

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

/* Commands */

extern const struct sieve_command cmd_setflag;
extern const struct sieve_command cmd_addflag;
extern const struct sieve_command cmd_removeflag;

/* Extension definitions */

int ext_imapflags_my_id;

extern const struct sieve_opcode setflag_opcode;
extern const struct sieve_opcode addflag_opcode;
extern const struct sieve_opcode removeflag_opcode;

const struct sieve_opcode *imapflags_opcodes[] = 
	{ &setflag_opcode, &addflag_opcode, &removeflag_opcode };
const struct sieve_extension imapflags_extension = { 
	"imapflags", 
	ext_imapflags_load,
	ext_imapflags_validator_load, 
	NULL, 
	NULL, 
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
	/* Register new command */
	sieve_validator_register_command(validator, &cmd_setflag);
	sieve_validator_register_command(validator, &cmd_addflag);
	sieve_validator_register_command(validator, &cmd_removeflag);

	return TRUE;
}


