/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */
 
#ifndef __EXT_DATE_COMMON_H
#define __EXT_DATE_COMMON_H

/*
 * Extension
 */
 
extern const struct sieve_extension date_extension;

/* 
 * Tests
 */

extern const struct sieve_command date_test;
extern const struct sieve_command currentdate_test;
 
/*
 * Operations
 */

enum ext_date_opcode {
	EXT_DATE_OPERATION_DATE,
	EXT_DATE_OPERATION_CURRENTDATE
};

extern const struct sieve_operation date_operation;
extern const struct sieve_operation currentdate_operation;

#endif /* __EXT_DATE_COMMON_H */
