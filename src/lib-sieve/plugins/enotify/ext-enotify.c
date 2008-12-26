/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension enotify
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-notify-12.txt
 * Implementation: almost full, mailto method only
 * Status: under development
 * 
 */
	
#include <stdio.h>

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "sieve-ext-variables.h"

#include "ext-enotify-common.h"

/*
 * Operations
 */

const struct sieve_operation *ext_enotify_operations[] = {
	&notify_operation,
	&valid_notify_method_operation,
	&notify_method_capability_operation
};

/* 
 * Extension
 */

static bool ext_enotify_load(void);
static void ext_enotify_unload(void);
static bool ext_enotify_validator_load(struct sieve_validator *valdtr);

static int ext_my_id = -1;

const struct sieve_extension enotify_extension = { 
	"enotify", 
	&ext_my_id,
	ext_enotify_load,
	ext_enotify_unload,
	ext_enotify_validator_load, 
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_OPERATIONS(ext_enotify_operations),
	SIEVE_EXT_DEFINE_OPERAND(encodeurl_operand)
};

static bool ext_enotify_load(void)
{
	ext_enotify_methods_init();

	sieve_extension_capabilities_register(&notify_capabilities);

	return TRUE;
}

static void ext_enotify_unload(void)
{
	ext_enotify_methods_deinit();
}

static bool ext_enotify_validator_load(struct sieve_validator *valdtr)
{
	/* Register new commands */
	sieve_validator_register_command(valdtr, &notify_command);
	sieve_validator_register_command(valdtr, &valid_notify_method_test);
	sieve_validator_register_command(valdtr, &notify_method_capability_test);
	
	/* Register new set modifier for variables extension */
	sieve_variables_modifier_register(valdtr, &encodeurl_modifier);
	
	return TRUE;
}

