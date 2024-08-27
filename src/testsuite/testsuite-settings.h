#ifndef TESTSUITE_SETTINGS_H
#define TESTSUITE_SETTINGS_H

#include "sieve-common.h"

void testsuite_settings_init(void);

void testsuite_setting_set(const char *identifier, const char *value);
void testsuite_setting_unset(const char *identifier);

#endif
