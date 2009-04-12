/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __EXT_ENVIRONMENT_COMMON_H
#define __EXT_ENVIRONMENT_COMMON_H

#include "sieve-common.h"

#include "sieve-ext-environment.h"

/*
 * Extension
 */

extern const struct sieve_extension environment_extension;

/* 
 * Commands 
 */

extern const struct sieve_command tst_environment;

/*
 * Operations
 */

extern const struct sieve_operation tst_environment_operation;

/*
 * Environment items
 */

extern const struct sieve_environment_item domain_env_item;
extern const struct sieve_environment_item host_env_item;
extern const struct sieve_environment_item location_env_item;
extern const struct sieve_environment_item phase_env_item;
extern const struct sieve_environment_item name_env_item;
extern const struct sieve_environment_item version_env_item;

/*
 * Initialization
 */

bool ext_environment_init(void);
void ext_environment_deinit(void);

/*
 * Environment item retrieval
 */

const char *ext_environment_item_get_value
	(const char *name, const struct sieve_script_env *senv);

#endif /* __EXT_VARIABLES_COMMON_H */
