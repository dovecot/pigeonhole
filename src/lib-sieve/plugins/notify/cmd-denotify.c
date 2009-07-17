/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"

#include "ext-notify-common.h"
 
/* Denotify command (NOT IMPLEMENTED)
 *
 * Syntax:
 *   denotify [MATCH-TYPE string] [<":low" / ":normal" / ":high">]
 */

static bool cmd_denotify_pre_validate
	(struct sieve_validator *valdtr, struct sieve_command_context *cmd);

const struct sieve_command cmd_denotify = {
	"denotify",
	SCT_COMMAND,
	0, 0, FALSE, FALSE,
	NULL,
	cmd_denotify_pre_validate,
	NULL, NULL, NULL
};

/* 
 * Denotify operation 
 */

const struct sieve_operation denotify_operation = { 
	"DENOTIFY",
	&notify_extension,
	EXT_NOTIFY_OPERATION_DENOTIFY,
	NULL, NULL
};

/*
 * Command validation
 */

static bool cmd_denotify_pre_validate
(struct sieve_validator *valdtr, struct sieve_command_context *cmd)
{
	sieve_command_validate_error(valdtr, cmd,
		"the denotify command cannot be used "
		"(the deprecated notify extension is not fully implemented)");
	return FALSE;
}


