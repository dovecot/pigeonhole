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
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-address-parts.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include <string.h>

/* Config */

#define SUBADDRESS_DEFAULT_SEP_CHAR '+'

/* Forward declarations */

static bool ext_subaddress_load(int ext_id);
static bool ext_subaddress_validator_load(struct sieve_validator *validator);
static bool ext_subaddress_interpreter_load
	(struct sieve_interpreter *interpreter);

/* Extension definitions */

static int ext_my_id;

const struct sieve_extension subaddress_extension = { 
	"subaddress", 
	ext_subaddress_load,
	ext_subaddress_validator_load,
	NULL, 
	ext_subaddress_interpreter_load,  
	SIEVE_EXT_DEFINE_NO_OPCODES, 
	NULL
};

static bool ext_subaddress_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Actual extension implementation */

static const char *subaddress_user_extract_from
	(const struct message_address *address)
{
	const char *sep = strchr(address->mailbox, SUBADDRESS_DEFAULT_SEP_CHAR);
	
	if ( sep == NULL ) return address->mailbox;
	
	return t_strdup_until(address->mailbox, sep);
}

static const char *subaddress_detail_extract_from
	(const struct message_address *address)
{
	const char *sep = strchr(address->mailbox, SUBADDRESS_DEFAULT_SEP_CHAR);

	if ( sep == NULL ) return NULL;
		
	return sep+1;
}

/* Extension access structures */

enum ext_subaddress_address_part {
  SUBADDRESS_USER,
  SUBADDRESS_DETAIL
};

extern const struct sieve_address_part_extension subaddress_addrp_extension;

const struct sieve_address_part user_address_part = {
	"user",
	SIEVE_ADDRESS_PART_CUSTOM,
	&subaddress_addrp_extension,
	SUBADDRESS_USER,
	subaddress_user_extract_from
};

const struct sieve_address_part detail_address_part = {
	"detail",
	SIEVE_ADDRESS_PART_CUSTOM,
	&subaddress_addrp_extension,
	SUBADDRESS_DETAIL,
	subaddress_detail_extract_from
};

static const struct sieve_address_part *ext_subaddress_get_part 
	(unsigned int code)
{
	switch ( code ) {
	case SUBADDRESS_USER:
		return &user_address_part;
	case SUBADDRESS_DETAIL:
		return &detail_address_part;
	default:
		break;
	}
	
	return NULL;
}

const struct sieve_address_part_extension subaddress_addrp_extension = { 
	&subaddress_extension,
	NULL, 
	ext_subaddress_get_part
};

/* Load extension into validator */

static bool ext_subaddress_validator_load(struct sieve_validator *validator)
{
	sieve_address_part_register
		(validator, &user_address_part, ext_my_id); 
	sieve_address_part_register
		(validator, &detail_address_part, ext_my_id); 

	return TRUE;
}

/* Load extension into interpreter */

static bool ext_subaddress_interpreter_load
	(struct sieve_interpreter *interpreter)
{
	sieve_address_part_extension_set
		(interpreter, ext_my_id, &subaddress_addrp_extension);

	return TRUE;
}


