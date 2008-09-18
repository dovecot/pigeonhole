/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __EXT_VARIABLES_DUMP_H
#define __EXT_VARIABLES_DUMP_H

#include "sieve-common.h"

/*
 * Code dump context
 */
 
bool ext_variables_code_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);


#endif /* __EXT_VARIABLES_DUMP_H */
