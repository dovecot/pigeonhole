/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-binary.h"

#include "sieve-validator.h"
#include "sieve-interpreter.h"

#include "ext-metadata-common.h"

/*
 * Extension mboxmetadata
 * -----------------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5490; Section 3
 * Implementation: skeleton
 * Status: development
 *
 */

const struct sieve_operation_def *mboxmetadata_operations[] = {
	&metadata_operation,
	&metadataexists_operation,
};

static bool ext_mboxmetadata_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);

const struct sieve_extension_def mboxmetadata_extension = {
	.name = "mboxmetadata",
	.validator_load = ext_mboxmetadata_validator_load,
	SIEVE_EXT_DEFINE_OPERATIONS(mboxmetadata_operations)
};

static bool ext_mboxmetadata_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	sieve_validator_register_command(valdtr, ext, &metadata_test);
	sieve_validator_register_command(valdtr, ext, &metadataexists_test);

	return TRUE;
}

/*
 * Extension servermetadata
 * -----------------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5490; Section 4
 * Implementation: skeleton
 * Status: development
 *
 */

const struct sieve_operation_def *servermetadata_operations[] = {
	&servermetadata_operation,
	&servermetadataexists_operation,
};

static bool ext_servermetadata_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);

const struct sieve_extension_def servermetadata_extension = {
	.name = "servermetadata",
	.validator_load = ext_servermetadata_validator_load,
	SIEVE_EXT_DEFINE_OPERATIONS(servermetadata_operations)
};

static bool ext_servermetadata_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	sieve_validator_register_command(valdtr, ext, &servermetadata_test);
	sieve_validator_register_command(valdtr, ext, &servermetadataexists_test);

	return TRUE;
}


