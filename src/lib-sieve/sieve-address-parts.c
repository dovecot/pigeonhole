#include <stdio.h>

#include "lib.h"
#include "compat.h"
#include "hash.h"
#include "array.h"

#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "sieve-address-parts.h"

#include <string.h>

/* 
 * Predeclarations 
 */
 
static void opr_address_part_emit
	(struct sieve_binary *sbin, unsigned int code);
static void opr_address_part_emit_ext
	(struct sieve_binary *sbin, int ext_id);

/* 
 * Comparator 'extension' 
 */

static int ext_my_id = -1;

static bool addrp_extension_load(int ext_id);
static bool addrp_validator_load(struct sieve_validator *validator);
static bool addrp_interpreter_load(struct sieve_interpreter *interp);

const struct sieve_extension address_part_extension = {
	"@address-part",
	addrp_extension_load,
	addrp_validator_load,
	NULL,
	NULL, //addrp_interpreter_load,
	NULL,
	NULL
};
	
static bool addrp_extension_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* 
 * Validator context:
 *   name-based comparator registry. 
 */
 
struct addrp_validator_context {
	struct hash_table *address_parts;
};

static inline struct addrp_validator_context *
	get_validator_context(struct sieve_validator *validator)
{
	return (struct addrp_validator_context *) 
		sieve_validator_extension_get_context(validator, ext_my_id);
}
 
void sieve_address_part_register
	(struct sieve_validator *validator, const struct sieve_address_part *addrp) 
{
	struct addrp_validator_context *ctx = get_validator_context(validator);
	
	hash_insert(ctx->address_parts, (void *) addrp->identifier, (void *) addrp);
}

const struct sieve_address_part *sieve_address_part_find
		(struct sieve_validator *validator, const char *addrp_name) 
{
	struct addrp_validator_context *ctx = get_validator_context(validator);

  return 	(const struct sieve_address_part *) 
  	hash_lookup(ctx->address_parts, addrp_name);
}

static bool addrp_validator_load(struct sieve_validator *validator)
{
	unsigned int i;
	pool_t pool = sieve_validator_pool(validator);
	
	struct addrp_validator_context *ctx = 
		p_new(pool, struct addrp_validator_context, 1);
	
	/* Setup comparator registry */
	ctx->address_parts = hash_create
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);

	/* Register core comparators */
	for ( i = 0; i < sieve_core_address_parts_count; i++ ) {
		const struct sieve_address_part *addrp = sieve_core_address_parts[i];
		
		hash_insert(ctx->address_parts, (void *) addrp->identifier, (void *) addrp);
	}

	sieve_validator_extension_set_context(validator, ext_my_id, ctx);

	return TRUE;
}

void sieve_address_parts_link_tags
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg,
		unsigned int id_code) 
{
	struct addrp_validator_context *ctx = get_validator_context(validator);
	struct hash_iterate_context *itx = hash_iterate_init(ctx->address_parts);
	void *key; 
	void *addrp;
	
	while ( hash_iterate(itx, &key, &addrp) ) {
		sieve_validator_register_tag
			(validator, cmd_reg, ((struct sieve_address_part *) addrp)->tag, id_code); 	
	}

	hash_iterate_deinit(&itx); 	
}

/*
 * Address-part operand
 */
 
struct sieve_operand_class address_part_class = 
	{ "address-part", NULL };
struct sieve_operand address_part_operand = 
	{ "address-part", &address_part_class, FALSE };

/* 
 * Address-part tag 
 */
 
static bool tag_address_part_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	const struct sieve_address_part *addrp;

	/* Syntax:   
	 *   ":localpart" / ":domain" / ":all"
   */
	
	/* Get address_part from registry */
	addrp = sieve_address_part_find(validator, sieve_ast_argument_tag(*arg));
	
	if ( addrp == NULL ) {
		sieve_command_validate_error(validator, cmd, 
			"unknown address-part modifier '%s' "
			"(this error should not occur and is probably a bug)", 
			sieve_ast_argument_strc(*arg));

		return FALSE;
	}

	/* Store comparator in context */
	(*arg)->context = (void *) addrp;
	
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/* Code generation */

static void opr_address_part_emit
	(struct sieve_binary *sbin, unsigned int code)
{ 
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_ADDRESS_PART);
	(void) sieve_binary_emit_byte(sbin, code);
}

static void opr_address_part_emit_ext
	(struct sieve_binary *sbin, int ext_id)
{ 
	unsigned char cmp_code = SIEVE_ADDRESS_PART_CUSTOM + 
		sieve_binary_extension_get_index(sbin, ext_id);
	
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_ADDRESS_PART);	
	(void) sieve_binary_emit_byte(sbin, cmp_code);
}

const struct sieve_address_part *sieve_opr_address_part_read
  (struct sieve_binary *sbin, sieve_size_t *address)
{
	unsigned int addrp_code;
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	
	if ( operand == NULL || operand->class != &address_part_class ) 
		return NULL;
	
	if ( sieve_binary_read_byte(sbin, address, &addrp_code) ) {
		if ( addrp_code < SIEVE_ADDRESS_PART_CUSTOM ) {
			if ( addrp_code < sieve_core_address_parts_count )
				return sieve_core_address_parts[addrp_code];
			else
				return NULL;
		} else {
		  /*const struct sieve_extension *ext = 
		  	sieve_binary_get_extension(sbin, cmp_code - SIEVE_ADDRESS_PART_CUSTOM);
		  
		  if ( ext != NULL )
		  	return (const struct sieve_address_part *) 
		  		sieve_interpreter_registry_get(addrp_registry, ext);	
		  else*/
		  	return NULL;
		}
	}		
		
	return NULL;
}

bool sieve_opr_address_part_dump(struct sieve_binary *sbin, sieve_size_t *address)
{
	sieve_size_t pc = *address;
	const struct sieve_address_part *addrp = 
		sieve_opr_address_part_read(sbin, address);
	
	if ( addrp == NULL )
		return FALSE;
		
	printf("%08x:   ADDRP: %s\n", pc, addrp->identifier);
	
	return TRUE;
}

static bool tag_address_part_generate
	(struct sieve_generator *generator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	struct sieve_address_part *addrp = 
		(struct sieve_address_part *) (*arg)->context;
	
	if ( addrp->extension == NULL ) {
		if ( addrp->code < SIEVE_ADDRESS_PART_CUSTOM )
			opr_address_part_emit(sbin, addrp->code);
		else
			return FALSE;
	} else {
		//opr_comparator_emit_ext(sbin, cmp->extension);
	} 
		
	*arg = sieve_ast_argument_next(*arg);
	
	return TRUE;
}

/* 
 * Core address-part modifiers
 */
 
const struct sieve_argument address_localpart_tag = 
	{ "localpart", tag_address_part_validate, tag_address_part_generate };
const struct sieve_argument address_domain_tag = 
	{ "domain", tag_address_part_validate, tag_address_part_generate };
const struct sieve_argument address_all_tag = 
	{ "all", tag_address_part_validate, tag_address_part_generate };

const struct sieve_address_part all_address_part = {
	"all",
	&address_all_tag,
	SIEVE_ADDRESS_PART_ALL,
	NULL
};

const struct sieve_address_part local_address_part = {
	"localpart",
	&address_localpart_tag,
	SIEVE_ADDRESS_PART_LOCAL,
	NULL
};

const struct sieve_address_part domain_address_part = {
	"domain",
	&address_domain_tag,
	SIEVE_ADDRESS_PART_DOMAIN,
	NULL
};

const struct sieve_address_part *sieve_core_address_parts[] = {
	&all_address_part, &local_address_part, &domain_address_part
};

const unsigned int sieve_core_address_parts_count = N_ELEMENTS(sieve_core_address_parts);


