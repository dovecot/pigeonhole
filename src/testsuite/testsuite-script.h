/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __TESTSUITE_SCRIPT_H
#define __TESTSUITE_SCRIPT_H

#include "sieve-common.h"

void testsuite_script_init(void);
void testsuite_script_deinit(void);

bool testsuite_script_compile(const char *script_path);
bool testsuite_script_run(const struct sieve_runtime_env *renv);

struct sieve_binary *testsuite_script_get_binary(void);
void testsuite_script_set_binary(struct sieve_binary *sbin);

#endif /* __TESTSUITE_SCRIPT_H */
