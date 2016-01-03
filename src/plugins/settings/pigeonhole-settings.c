/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "pigeonhole-config.h"
#include "pigeonhole-version.h"

/* This is currently just a dummy plugin that adds a Pigeonhole
 * version banner the doveconf output.
 */

const char *pigeonhole_settings_version = DOVECOT_ABI_VERSION;
const char *pigeonhole_settings_doveconf_banner = "Pigeonhole version "PIGEONHOLE_VERSION_FULL;
