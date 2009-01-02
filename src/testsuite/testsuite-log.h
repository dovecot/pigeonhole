/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __TESTSUITE_LOG_H
#define __TESTSUITE_LOG_H

#include "sieve-common.h"

extern struct sieve_error_handler *testsuite_log_ehandler;

void testsuite_log_init(void);
void testsuite_log_deinit(void);

void testsuite_log_clear_messages(void);
void testsuite_log_get_error_init(void);
const char *testsuite_log_get_error_next(bool location);

#endif /* __TESTSUITE_LOG_H */
