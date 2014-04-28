/* Copyright (c) 2002-2014 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_SMTP_H
#define __SIEVE_SMTP_H

#include "sieve-common.h"

bool sieve_smtp_available
	(const struct sieve_script_env *senv);

// FIXME: support multiple recipients
void *sieve_smtp_open
	(const struct sieve_script_env *senv, const char *destination,
   	const char *return_path, struct ostream **output_r);
int sieve_smtp_close
	(const struct sieve_script_env *senv, void *handle,
		const char **error_r);

#endif /* __SIEVE_SMTP_H */
