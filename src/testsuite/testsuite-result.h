/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __TESTSUITE_RESULT_H
#define __TESTSUITE_RESULT_H

void testsuite_result_init(void);
void testsuite_result_deinit(void);

void testsuite_result_assign(struct sieve_result *result);

struct sieve_result_iterate_context *testsuite_result_iterate_init(void);

#endif /* __TESTSUITE_RESULT_H */
