#ifndef TESTSUITE_SMTP_H
#define TESTSUITE_SMTP_H

void testsuite_smtp_init(void);
void testsuite_smtp_deinit(void);
void testsuite_smtp_reset(void);

/*
 * Simulated SMTP out
 */

void *testsuite_smtp_start
	(const struct sieve_script_env *senv ATTR_UNUSED,
		const struct smtp_address *mail_from);
void testsuite_smtp_add_rcpt
	(const struct sieve_script_env *senv ATTR_UNUSED,
		void *handle, const struct smtp_address *rcpt_to);
struct ostream *testsuite_smtp_send
	(const struct sieve_script_env *senv ATTR_UNUSED,
		void *handle);
void testsuite_smtp_abort
	(const struct sieve_script_env *senv ATTR_UNUSED,
		void *handle);
int testsuite_smtp_finish
	(const struct sieve_script_env *senv ATTR_UNUSED,
		void *handle, const char **error_r);

/*
 * Access
 */

bool testsuite_smtp_get
	(const struct sieve_runtime_env *renv, unsigned int index);

#endif
