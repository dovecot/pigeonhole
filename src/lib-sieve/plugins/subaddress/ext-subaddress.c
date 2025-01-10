/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension subaddress
 * --------------------
 *
 * Author: Stephan Bosch
 * Specification: RFC 3598
 * Implementation: full
 * Status: testing
 *
 */

#include "sieve-common.h"

#include "sieve-settings.h"
#include "sieve-code.h"
#include "sieve-address.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-address-parts.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include <string.h>

/*
 * Configuration
 */

#define SUBADDRESS_DEFAULT_DELIM "+"

struct ext_subaddress_config {
	char *delimiter;
};

/*
 * Forward declarations
 */

const struct sieve_address_part_def user_address_part;
const struct sieve_address_part_def detail_address_part;

static struct sieve_operand_def subaddress_operand;

/*
 * Extension
 */

static bool
ext_subaddress_load(const struct sieve_extension *ext, void **context);
static void ext_subaddress_unload(const struct sieve_extension *ext);
static bool
ext_subaddress_validator_load(const struct sieve_extension *ext,
			      struct sieve_validator *validator);

const struct sieve_extension_def subaddress_extension = {
	.name = "subaddress",
	.load = ext_subaddress_load,
	.unload = ext_subaddress_unload,
	.validator_load = ext_subaddress_validator_load,
	SIEVE_EXT_DEFINE_OPERAND(subaddress_operand),
};

static bool
ext_subaddress_load(const struct sieve_extension *ext, void **context)
{
	struct ext_subaddress_config *config;
	const char *delim;

	if (*context != NULL)
		ext_subaddress_unload(ext);

	delim = sieve_setting_get(ext->svinst, "recipient_delimiter");

	if (delim == NULL)
		delim = SUBADDRESS_DEFAULT_DELIM;

	config = i_new(struct ext_subaddress_config, 1);
	config->delimiter = i_strdup(delim);

	*context = config;
	return TRUE;
}

static void ext_subaddress_unload(const struct sieve_extension *ext)
{
	struct ext_subaddress_config *config =
		(struct ext_subaddress_config *)ext->context;

	i_free(config->delimiter);
	i_free(config);
}

static bool
ext_subaddress_validator_load(const struct sieve_extension *ext,
			      struct sieve_validator *validator)
{
	sieve_address_part_register(validator, ext, &user_address_part);
	sieve_address_part_register(validator, ext, &detail_address_part);

	return TRUE;
}

/*
 * Address parts
 */

enum ext_subaddress_address_part {
	SUBADDRESS_USER,
	SUBADDRESS_DETAIL
};

/* Forward declarations */

static const char *
subaddress_user_extract_from(const struct sieve_address_part *addrp,
			     const struct smtp_address *address);
static const char *
subaddress_detail_extract_from(const struct sieve_address_part *addrp,
			       const struct smtp_address *address);

/* Address part objects */

const struct sieve_address_part_def user_address_part = {
	SIEVE_OBJECT("user", &subaddress_operand, SUBADDRESS_USER),
	subaddress_user_extract_from,
};

const struct sieve_address_part_def detail_address_part = {
	SIEVE_OBJECT("detail", &subaddress_operand, SUBADDRESS_DETAIL),
	.extract_from = subaddress_detail_extract_from,
};

/* Address part implementation */

static const char *
subaddress_user_extract_from(const struct sieve_address_part *addrp,
			     const struct smtp_address *address)
{
	struct ext_subaddress_config *config =
		(struct ext_subaddress_config *)addrp->object.ext->context;
	const char *delim;
	size_t idx;

	idx = strcspn(address->localpart, config->delimiter);
	delim = (address->localpart[idx] != '\0' ?
		 address->localpart + idx : NULL);

	if (delim == NULL)
		return address->localpart;

	return t_strdup_until(address->localpart, delim);
}

static const char *
subaddress_detail_extract_from(const struct sieve_address_part *addrp,
			       const struct smtp_address *address)
{
	struct ext_subaddress_config *config =
		(struct ext_subaddress_config *)addrp->object.ext->context;
	const char *delim;
	size_t idx;

	idx = strcspn(address->localpart, config->delimiter);
	delim = (address->localpart[idx] != '\0' ?
		 address->localpart + idx + 1: NULL);

	/* Just to be sure */
	if (delim == NULL ||
	    delim > (address->localpart + strlen(address->localpart)))
		return NULL;
	return delim;
}

/*
 * Operand
 */

const struct sieve_address_part_def *ext_subaddress_parts[] = {
	&user_address_part,
	&detail_address_part,
};

static const struct sieve_extension_objects ext_address_parts =
	SIEVE_EXT_DEFINE_ADDRESS_PARTS(ext_subaddress_parts);

static struct sieve_operand_def subaddress_operand = {
	.name = "address-part",
	.ext_def = &subaddress_extension,
	.class = &sieve_address_part_operand_class,
	.interface = &ext_address_parts,
};
