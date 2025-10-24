/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "settings-history.h"
#include "pigeonhole-config.h"
#include "pigeonhole-version.h"

#include "settings-history-pigeonhole.h"
#include "pigeonhole-settings.h"

/* This adds a Pigeonhole version banner the doveconf output and registers
   settings history. */

void pigeonhole_settings_init(void);

void pigeonhole_settings_init(void)
{
	settings_history_register_renames(settings_history_pigeonhole_renames,
		N_ELEMENTS(settings_history_pigeonhole_renames));
	settings_history_register_defaults(settings_history_pigeonhole_defaults,
		N_ELEMENTS(settings_history_pigeonhole_defaults));
}

const char *pigeonhole_settings_version = DOVECOT_ABI_VERSION;
const char *pigeonhole_settings_doveconf_banner =
	"Pigeonhole version "PIGEONHOLE_VERSION_FULL;
