/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension subaddress 
 * --------------------
 *
 * Author: Stephan Bosch
 * Specification: RFC 3598
 * Implementation: full, but not configurable
 * Status: experimental, largely untested
 * 
 * FIXME: This extension is not configurable in any way. The separation 
 * character is currently only configurable for compilation and not at runtime. 
 *
 */
 
#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-address.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-address-parts.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include <stdlib.h>
#include <string.h>

/* 
 * Configuration 
 */

#define SUBADDRESS_DEFAULT_SEP "+"

static const char *sieve_subaddress_sep = SUBADDRESS_DEFAULT_SEP;

/*
 * Forward declarations 
 */

const struct sieve_address_part user_address_part;
const struct sieve_address_part detail_address_part;

static struct sieve_operand subaddress_operand;

/*
 * Extension
 */

static bool ext_subaddress_load(void);
static bool ext_subaddress_validator_load(struct sieve_validator *validator);

static int ext_my_id = -1;

const struct sieve_extension subaddress_extension = { 
	"subaddress", 
	&ext_my_id,
	ext_subaddress_load, 
	NULL,
	ext_subaddress_validator_load,
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_OPERAND(subaddress_operand)
};

static bool ext_subaddress_load(void)
{
	sieve_subaddress_sep = getenv("SIEVE_SUBADDRESS_SEP");

	if ( sieve_subaddress_sep == NULL )
		sieve_subaddress_sep = SUBADDRESS_DEFAULT_SEP;

	return TRUE;
}

static bool ext_subaddress_validator_load(struct sieve_validator *validator)
{
	sieve_address_part_register(validator, &user_address_part); 
	sieve_address_part_register(validator, &detail_address_part); 

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

static const char *subaddress_user_extract_from
	(const struct sieve_address *address);
static const char *subaddress_detail_extract_from
	(const struct sieve_address *address);


/* Address part objects */	

const struct sieve_address_part user_address_part = {
	SIEVE_OBJECT("user", &subaddress_operand, SUBADDRESS_USER),
	subaddress_user_extract_from
};

const struct sieve_address_part detail_address_part = {
	SIEVE_OBJECT("detail", &subaddress_operand, SUBADDRESS_DETAIL),
	subaddress_detail_extract_from
};

/* Address part implementation */

static const char *subaddress_user_extract_from
	(const struct sieve_address *address)
{
	const char *sep;

	sep = strstr(address->local_part, sieve_subaddress_sep);
	
	if ( sep == NULL ) return address->local_part;
	
	return t_strdup_until(address->local_part, sep);
}

static const char *subaddress_detail_extract_from
	(const struct sieve_address *address)
{
	const char *sep;

	if ( (sep=strstr(address->local_part, sieve_subaddress_sep)) == NULL )
		return NULL; 

	sep += strlen(sieve_subaddress_sep);

	/* Just to be sure */
	if ( sep > (address->local_part + strlen(address->local_part)) ) 
		return NULL;

	return sep;
}

/*
 * Operand 
 */

const struct sieve_address_part *ext_subaddress_parts[] = {
	&user_address_part, &detail_address_part
};

static const struct sieve_extension_objects ext_address_parts =
	SIEVE_EXT_DEFINE_ADDRESS_PARTS(ext_subaddress_parts);

static struct sieve_operand subaddress_operand = { 
	"address-part", 
	&subaddress_extension, 0,
	&sieve_address_part_operand_class,
	&ext_address_parts
};

