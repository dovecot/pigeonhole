/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension enotify
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-notify-12.txt
 * Implementation: skeleton
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

#include "ext-enotify-common.h"

/*
 * Operations
 */

const struct sieve_operation *ext_enotify_operations[] = {
	&notify_operation,
	&valid_notify_method_operation
};

/* 
 * Extension
 */

static bool ext_enotify_load(int ext_id);
static bool ext_enotify_validator_load(struct sieve_validator *validator);

static int ext_my_id;

const struct sieve_extension enotify_extension = { 
	"enotify", 
	&ext_my_id,
	ext_enotify_load,
	ext_enotify_validator_load, 
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_OPERATIONS(ext_enotify_operations),
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_enotify_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

static bool ext_enotify_validator_load(struct sieve_validator *validator)
{
	/* Register new commands */
	sieve_validator_register_command(validator, &notify_command);
	sieve_validator_register_command(validator, &valid_notify_method_test);
	
	return TRUE;
}

