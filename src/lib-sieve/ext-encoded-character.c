/* Extension encoded-character 
 * ---------------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-3028bis-13.txt
 * Implementation: skeleton  
 * Status: under development
 *
 */

#include "lib.h"

#include "sieve-extensions.h"
#include "sieve-validator.h"

/* Forward declarations */

static bool ext_encoded_character_load(int ext_id);
static bool ext_encoded_character_validator_load(struct sieve_validator *validator);

/* Extension definitions */

static int ext_my_id;
	
struct sieve_extension encoded_character_extension = { 
	"encoded-character", 
	ext_encoded_character_load,
	ext_encoded_character_validator_load, 
	NULL, NULL, NULL, 
	SIEVE_EXT_DEFINE_NO_OPCODES, 
	NULL 
};

static bool ext_encoded_character_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* Load extension into validator */

static bool ext_encoded_character_validator_load
	(struct sieve_validator *validator ATTR_UNUSED)
{
	return TRUE;
}
