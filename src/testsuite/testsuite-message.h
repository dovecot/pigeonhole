/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __TESTSUITE_MESSAGE_H
#define __TESTSUITE_MESSAGE_H

#include "sieve-common.h"

extern struct sieve_message_data testsuite_msgdata;

void testsuite_message_init(const char *user);
void testsuite_message_deinit(void);

void testsuite_message_set
	(const struct sieve_runtime_env *renv, string_t *message);

void testsuite_envelope_set_sender(const char *value);
void testsuite_envelope_set_recipient(const char *value);
void testsuite_envelope_set_auth_user(const char *value);

#endif /* __TESTSUITE_MESSAGE_H */
