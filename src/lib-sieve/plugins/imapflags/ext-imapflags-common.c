#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"

bool ext_imapflags_command_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd)
{
	return TRUE;
}

