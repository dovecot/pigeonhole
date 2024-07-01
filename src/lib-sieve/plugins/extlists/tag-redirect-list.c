/* Copyright (c) 2025 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mail-storage.h"
#include "mail-namespace.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-result.h"
#include "sieve-validator.h"
#include "sieve-generator.h"

#include "ext-extlists-common.h"

/*
 * Tagged argument
 */

static bool
tag_redirect_list_validate(struct sieve_validator *valdtr,
			   struct sieve_ast_argument **arg,
			   struct sieve_command *cmd);

const struct sieve_argument_def redirect_list_tag = {
	.identifier = "list",
	.validate = tag_redirect_list_validate,
};

/*
 * Tag validation
 */

static bool
tag_redirect_list_validate(struct sieve_validator *valdtr ATTR_UNUSED,
			   struct sieve_ast_argument **arg,
			   struct sieve_command *cmd ATTR_UNUSED)
{
	sieve_argument_validate_error(
		valdtr, *arg, "list tag: "
		"using :list with redirect is not supported");
	return FALSE;
}
