/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
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
	(const struct sieve_script_env *senv ATTR_UNUSED, const char *destination,
		const char *return_path, struct ostream **output_r);
bool testsuite_smtp_close
	(const struct sieve_script_env *senv, void *handle);

/*
 * Access
 */

bool testsuite_smtp_get
	(const struct sieve_runtime_env *renv, unsigned int index);

#endif /* __TESTSUITE_SMTP_H */
