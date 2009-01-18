/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */
 
#ifndef __TESTSUITE_SMTP_H
#define __TESTSUITE_SMTP_H
 
void testsuite_smtp_init(void);
void testsuite_smtp_deinit(void);
void testsuite_smtp_reset(void);

/*
 * Simulated SMTP out
 */
 
void *testsuite_smtp_open
	(const char *destination, const char *return_path, FILE **file_r);
bool testsuite_smtp_close(void *handle);

#endif /* __TESTSUITE_SMTP_H */
