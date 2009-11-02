/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_TOOL_H
#define __SIEVE_TOOL_H

#include "sieve-common.h"

/* Functionality common to all Sieve command line tools. */

/*
 * Sieve instance
 */

struct sieve_instance *sieve_instance;

/*
 * Initialization
 */

void sieve_tool_init(sieve_settings_func_t settings_func, bool init_lib);
void sieve_tool_deinit(void);

/*
 * Commonly needed functionality
 */

const char *sieve_tool_get_user(void);

void sieve_tool_get_envelope_data
	(struct mail *mail, const char **recipient, const char **sender);

/*
 * Sieve script handling
 */

struct sieve_binary *sieve_tool_script_compile
	(const char *filename, const char *name);
struct sieve_binary *sieve_tool_script_open(const char *filename);
void sieve_tool_dump_binary_to(struct sieve_binary *sbin, const char *filename);

#endif /* __SIEVE_TOOL_H */
