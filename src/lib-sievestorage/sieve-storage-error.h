/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_STORAGE_ERROR_H
#define __SIEVE_STORAGE_ERROR_H

enum sieve_storage_error {
	SIEVE_STORAGE_ERROR_NONE = 0,

	/* Temporary internal error */
	SIEVE_STORAGE_ERROR_TEMP,

	/* It's not possible to do the wanted operation */
	SIEVE_STORAGE_ERROR_IMPOSSIBLE,

	/* Quota exceeded */
	SIEVE_STORAGE_ERROR_QUOTA,

	/* Out of disk space */	
	SIEVE_STORAGE_ERROR_NOSPACE,

	/* Script does not exist */
	SIEVE_STORAGE_ERROR_NOTFOUND,

	/* Operation not allowed on active script */
	SIEVE_STORAGE_ERROR_ACTIVE,

	/* Operation not allowed on existing script */
	SIEVE_STORAGE_ERROR_EXISTS
};

#endif /* __SIEVE_STORAGE_ERROR_H */
