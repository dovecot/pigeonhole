/* Copyright (c) 2016-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension report
 * ----------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-report-00.txt
 * Implementation: full, but deprecated; provided for backwards compatibility
 * Status: testing
 *
 */

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "ext-vnd-report-common.h"

/*
 * Extension
 */

static bool
ext_report_validator_load(const struct sieve_extension *ext,
			  struct sieve_validator *valdtr);

const struct sieve_extension_def vnd_report_extension = {
	.name = "vnd.dovecot.report",
	.load = ext_report_load,
	.unload = ext_report_unload,
	.validator_load = ext_report_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(report_operation),
};

/*
 * Extension validation
 */

static bool
ext_report_validator_load(const struct sieve_extension *ext,
			  struct sieve_validator *valdtr)
{
	/* Register new commands */
	sieve_validator_register_command(valdtr, ext, &cmd_report);

	return TRUE;
}
