/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __TESTSUITE_SETTINGS_H
#define __TESTSUITE_SETTINGS_H

#include "sieve-common.h"

void testsuite_settings_init(void);
void testsuite_settings_deinit(void);

const char *testsuite_setting_get(void *context, const char *identifier);
void testsuite_setting_set(const char *identifier, const char *value);

#endif /* __TESTSUITE_SETTINGS_H */
