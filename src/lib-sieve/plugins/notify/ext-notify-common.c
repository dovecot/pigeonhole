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

/*
 * Importance argument
 */

static bool tag_importance_validate
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command_context *cmd);

static const struct sieve_argument importance_low_tag = {
	"low",
	NULL, NULL,
	tag_importance_validate,
	NULL, NULL
};

static const struct sieve_argument importance_normal_tag = {
	"normal",
	NULL, NULL,
	tag_importance_validate,
	NULL, NULL
};

static const struct sieve_argument importance_high_tag = {
	"high",
	NULL, NULL,
	tag_importance_validate,
	NULL, NULL
};

static bool tag_importance_validate
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_ast_argument **arg,
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_ast_argument *tag = *arg;

	if ( tag->argument == &importance_low_tag )
		sieve_ast_argument_number_substitute(tag, 3);
	else if ( tag->argument == &importance_normal_tag )
		sieve_ast_argument_number_substitute(tag, 2);
	else
		sieve_ast_argument_number_substitute(tag, 1);

	tag->argument = &number_argument;

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

void ext_notify_register_importance_tags
(struct sieve_validator *valdtr, struct sieve_command_registration *cmd_reg,
	unsigned int id_code)
{
	sieve_validator_register_tag(valdtr, cmd_reg, &importance_low_tag, id_code);
	sieve_validator_register_tag(valdtr, cmd_reg, &importance_normal_tag, id_code);
	sieve_validator_register_tag(valdtr, cmd_reg, &importance_high_tag, id_code);
}

