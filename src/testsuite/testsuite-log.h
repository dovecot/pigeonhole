#ifndef TESTSUITE_LOG_H
#define TESTSUITE_LOG_H

#include "sieve-common.h"

extern struct sieve_error_handler *testsuite_log_ehandler;
extern struct sieve_error_handler *testsuite_log_main_ehandler;

/*
 * Initialization
 */

void testsuite_log_init(bool log_stdout);
void testsuite_log_deinit(void);

/*
 * Access
 */

void testsuite_log_clear_messages(void);

struct sieve_stringlist *
testsuite_log_stringlist_create(const struct sieve_runtime_env *renv,
				int index);

#endif
