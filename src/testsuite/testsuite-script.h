#ifndef TESTSUITE_SCRIPT_H
#define TESTSUITE_SCRIPT_H

#include "sieve-common.h"

void testsuite_script_init(void);
void testsuite_script_deinit(void);

const char *testsuite_script_get_name(const char *path);

bool testsuite_script_is_subtest(const struct sieve_runtime_env *renv);

bool testsuite_script_compile(const struct sieve_runtime_env *renv,
			      const char *script);
bool testsuite_script_run(const struct sieve_runtime_env *renv);
bool testsuite_script_multiscript(const struct sieve_runtime_env *renv,
				  ARRAY_TYPE (const_string) *scriptfiles);

struct sieve_binary *
testsuite_script_get_binary(const struct sieve_runtime_env *renv);
void testsuite_script_set_binary(const struct sieve_runtime_env *renv,
				 struct sieve_binary *sbin);

#endif
