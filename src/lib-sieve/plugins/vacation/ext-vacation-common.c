/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "settings.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-extensions.h"

#include "ext-vacation-common.h"

int ext_vacation_load(const struct sieve_extension *ext, void **context)
{
	struct sieve_instance *svinst = ext->svinst;
	const struct ext_vacation_settings *set;
	struct ext_vacation_context *extctx;
	const char *error;

	if (*context != NULL) {
		ext_vacation_unload(ext);
		*context = NULL;
	}

	if (settings_get(svinst->event, &ext_vacation_setting_parser_info, 0,
			 &set, &error) < 0) {
		e_error(svinst->event, "%s", error);
		return -1;
	}

	extctx = i_new(struct ext_vacation_context, 1);
	extctx->set = set;

	*context = extctx;
	return 0;
}

void ext_vacation_unload(const struct sieve_extension *ext)
{
	struct ext_vacation_context *extctx = ext->context;

	if (extctx == NULL)
		return;
	settings_free(extctx->set);
	i_free(extctx);
}
