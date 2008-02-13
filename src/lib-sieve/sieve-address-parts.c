#include <stdio.h>

#include "lib.h"
#include "compat.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"
#include "message-address.h"

#include "sieve-extensions-private.h"
#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-binary.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

#include "sieve-address-parts.h"

#include <string.h>

/* 
 * Default address parts
 */

const struct sieve_address_part *sieve_core_address_parts[] = {
	&all_address_part, &local_address_part, &domain_address_part
};

const unsigned int sieve_core_address_parts_count = 
	N_ELEMENTS(sieve_core_address_parts);

static struct sieve_extension_obj_registry addrp_default_reg =
	SIEVE_EXT_DEFINE_ADDRESS_PARTS(sieve_core_address_parts);

/* 
 * Forward declarations 
 */
  
static void opr_address_part_emit
	(struct sieve_binary *sbin, const struct sieve_address_part *addrp, 
		int ext_id);

/* 
 * Address-part 'extension' 
 */

static int ext_my_id = -1;

static bool addrp_extension_load(int ext_id);
static bool addrp_validator_load(struct sieve_validator *validator);
static bool addrp_binary_load(struct sieve_binary *sbin);

const struct sieve_extension address_part_extension = {
	"@address-parts",
	addrp_extension_load,
	addrp_validator_load,
	NULL, 
	addrp_binary_load,
	NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS,
	SIEVE_EXT_DEFINE_NO_OPERANDS
};
	
static bool addrp_extension_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* 
 * Validator context:
 *   name-based address-part registry. 
 *
 * FIXME: This code will be duplicated across all extensions that introduce 
 * a registry of some kind in the validator. 
 */
 
struct addrp_validator_registration {
	int ext_id;
	const struct sieve_address_part *address_part;
};
 
struct addrp_validator_context {
	struct hash_table *registrations;
};

static inline struct addrp_validator_context *
	get_validator_context(struct sieve_validator *validator)
{
	return (struct addrp_validator_context *) 
		sieve_validator_extension_get_context(validator, ext_my_id);
}

static void _sieve_address_part_register
	(pool_t pool, struct addrp_validator_context *ctx, 
	const struct sieve_address_part *addrp, int ext_id) 
{
	struct addrp_validator_registration *reg;
	
	reg = p_new(pool, struct addrp_validator_registration, 1);
	reg->address_part = addrp;
	reg->ext_id = ext_id;
	
	hash_insert(ctx->registrations, (void *) addrp->identifier, (void *) reg);
}
 
void sieve_address_part_register
	(struct sieve_validator *validator, 
	const struct sieve_address_part *addrp, int ext_id) 
{
	pool_t pool = sieve_validator_pool(validator);
	struct addrp_validator_context *ctx = get_validator_context(validator);

	_sieve_address_part_register(pool, ctx, addrp, ext_id);
}

const struct sieve_address_part *sieve_address_part_find
	(struct sieve_validator *validator, const char *identifier,
		int *ext_id) 
{
	struct addrp_validator_context *ctx = get_validator_context(validator);
	struct addrp_validator_registration *reg =
		(struct addrp_validator_registration *) 
			hash_lookup(ctx->registrations, identifier);
			
	if ( reg == NULL ) return NULL;

	if ( ext_id != NULL ) *ext_id = reg->ext_id;

  return reg->address_part;
}

bool addrp_validator_load(struct sieve_validator *validator)
{
	unsigned int i;
	pool_t pool = sieve_validator_pool(validator);
	
	struct addrp_validator_context *ctx = 
		p_new(pool, struct addrp_validator_context, 1);
	
	/* Setup address-part registry */
	ctx->registrations = hash_create
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);

	/* Register core address-parts */
	for ( i = 0; i < sieve_core_address_parts_count; i++ ) {
		const struct sieve_address_part *addrp = sieve_core_address_parts[i];
		
		_sieve_address_part_register(pool, ctx, addrp, -1);
	}

	sieve_validator_extension_set_context(validator, ext_my_id, ctx);

	return TRUE;
}

void sieve_address_parts_link_tags
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg, int id_code) 
{	
	sieve_validator_register_tag
		(validator, cmd_reg, &address_part_tag, id_code); 	
}

/*
 * Binary context
 */

static inline const struct sieve_address_part_extension *
	sieve_address_part_extension_get(struct sieve_binary *sbin, int ext_id)
{
	return (const struct sieve_address_part_extension *)
		sieve_binary_registry_get_object(sbin, ext_my_id, ext_id);
}

void sieve_address_part_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_address_part_extension *ext)
{
	sieve_binary_registry_set_object
		(sbin, ext_my_id, ext_id, (const void *) ext);
}

static bool addrp_binary_load(struct sieve_binary *sbin)
{
	sieve_binary_registry_init(sbin, ext_my_id);
	
	return TRUE;
}

/*
 * Address-part operand
 */
 
struct sieve_operand_class address_part_class = 
	{ "address-part" };

struct sieve_operand address_part_operand = { 
	"address-part", 
	NULL, SIEVE_OPERAND_ADDRESS_PART,
	&address_part_class,
	NULL
};

/* 
 * Address-part tag 
 */
  
static bool tag_address_part_is_instance_of
(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *arg)
{
	int ext_id;
	struct sieve_address_part_context *adpctx;
	const struct sieve_address_part *addrp = sieve_address_part_find
		(validator, sieve_ast_argument_tag(arg), &ext_id);

	if ( addrp == NULL ) return FALSE;

	adpctx = p_new(sieve_command_pool(cmd), struct sieve_address_part_context, 1);
	adpctx->command_ctx = cmd;
	adpctx->address_part = addrp;
	adpctx->ext_id = ext_id;

	/* Store address-part in context */
	arg->context = (void *) adpctx;

	return TRUE;
}
 
static bool tag_address_part_validate
(struct sieve_validator *validator ATTR_UNUSED, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	/* FIXME: Currenly trivial, but might need to allow for further validation for
	 * future extensions.
	 */
	 
	/* Syntax:   
	 *   ":localpart" / ":domain" / ":all" (subject to extension)
   */
	
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/* 
 * Code generation 
 */

static void opr_address_part_emit
(struct sieve_binary *sbin, const struct sieve_address_part *addrp, int ext_id)
{
	(void) sieve_operand_emit_code(sbin, &address_part_operand, -1);	

	(void) sieve_extension_emit_obj
		(sbin, &addrp_default_reg, addrp, address_parts, ext_id);
}

static const struct sieve_extension_obj_registry *
	sieve_address_part_registry_get
(struct sieve_binary *sbin, unsigned int ext_index)
{
	int ext_id = -1; 
	const struct sieve_address_part_extension *ext;
	
	if ( sieve_binary_extension_get_by_index(sbin, ext_index, &ext_id) == NULL )
		return NULL;

	if ( (ext=sieve_address_part_extension_get(sbin, ext_id)) == NULL ) 
		return NULL;
		
	return &(ext->address_parts);
}

static inline const struct sieve_address_part *sieve_address_part_read
(struct sieve_binary *sbin, sieve_size_t *address)
{
	return sieve_extension_read_obj
		(struct sieve_address_part, sbin, address, &addrp_default_reg, 
			sieve_address_part_registry_get);
}

const struct sieve_address_part *sieve_opr_address_part_read
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);
	
	if ( operand == NULL || operand->class != &address_part_class ) 
		return NULL;

	return sieve_address_part_read(renv->sbin, address);
}

bool sieve_opr_address_part_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct sieve_operand *operand = sieve_operand_read(denv->sbin, address);
	const struct sieve_address_part *addrp; 
	
	if ( operand == NULL || operand->class != &address_part_class ) 
		return NULL;
		
	sieve_code_mark(denv);
	addrp = sieve_address_part_read(denv->sbin, address);
	
	if ( addrp == NULL )
		return FALSE;
		
	sieve_code_dumpf(denv, "ADDRESS-PART: %s", addrp->identifier);
	
	return TRUE;
}

static bool tag_address_part_generate
	(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	struct sieve_address_part_context *adpctx =
		(struct sieve_address_part_context *) arg->context;
	const struct sieve_address_part *addrp = adpctx->address_part;	
	
	if ( addrp->extension == NULL ) {
		if ( addrp->code < SIEVE_ADDRESS_PART_CUSTOM )
			opr_address_part_emit(sbin, addrp, -1);
		else
			return FALSE;
	} else {
		opr_address_part_emit(sbin, addrp, adpctx->ext_id);
	} 
		
	return TRUE;
}

/*
 * Address Matching
 */
 
bool sieve_address_match
(const struct sieve_address_part *addrp, struct sieve_match_context *mctx, 		
	const char *data)
{
	bool matched = FALSE;
	const struct message_address *addr;

	T_BEGIN {
		addr = message_address_parse
			(pool_datastack_create(), (const unsigned char *) data, 
				strlen(data), 256, FALSE);
	
		while (!matched && addr != NULL) {
			if (addr->domain != NULL) {
				/* mailbox@domain */
				const char *part;
			
				i_assert(addr->mailbox != NULL);

				part = addrp->extract_from(addr);
			
				if ( part != NULL && sieve_match_value(mctx, part, strlen(part)) )
					matched = TRUE;				
			} 

			addr = addr->next;
		}
	} T_END;
	
	return matched;
}

/* 
 * Default ADDRESS-PART, MATCH-TYPE, COMPARATOR access
 */
 
bool sieve_addrmatch_default_dump_optionals
(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	int opt_code = 1;
	
	if ( sieve_operand_optional_present(denv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(denv->sbin, address, &opt_code) ) 
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case SIEVE_AM_OPT_COMPARATOR:
				if ( !sieve_opr_comparator_dump(denv, address) )
					return FALSE;
				break;
			case SIEVE_AM_OPT_MATCH_TYPE:
				if ( !sieve_opr_match_type_dump(denv, address) )
					return FALSE;
				break;
			case SIEVE_AM_OPT_ADDRESS_PART:
				if ( !sieve_opr_address_part_dump(denv, address) )
					return FALSE;
				break;
			default:
				return FALSE;
			}
		}
	}
	
	return TRUE;
}

bool sieve_addrmatch_default_get_optionals
(const struct sieve_runtime_env *renv, sieve_size_t *address, 
	const struct sieve_address_part **addrp, const struct sieve_match_type **mtch, 
	const struct sieve_comparator **cmp) 
{
	int opt_code = 1;
	
	
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) )
				return FALSE;
				  
			switch ( opt_code ) {
			case 0:
				break;
			case SIEVE_AM_OPT_COMPARATOR:
				if ( (*cmp = sieve_opr_comparator_read(renv, address)) == NULL )
					return FALSE;
				break;
			case SIEVE_AM_OPT_MATCH_TYPE:
				if ( (*mtch = sieve_opr_match_type_read(renv, address)) == NULL )
					return FALSE;
				break;
			case SIEVE_AM_OPT_ADDRESS_PART:
				if ( (*addrp = sieve_opr_address_part_read(renv, address)) == NULL )
					return FALSE;
				break;
			default:
				return FALSE;
			}
		}
	}
	
	return TRUE;
}

/* 
 * Core address-part modifiers
 */

const struct sieve_argument address_part_tag = { 
	"ADDRESS-PART",
	tag_address_part_is_instance_of, 
	tag_address_part_validate,
	NULL, 
	tag_address_part_generate 
};
 
static const char *addrp_all_extract_from
	(const struct message_address *address)
{
	return t_strconcat(address->mailbox, "@", address->domain, NULL);
}

static const char *addrp_domain_extract_from
	(const struct message_address *address)
{
	return address->domain;
}

static const char *addrp_localpart_extract_from
	(const struct message_address *address)
{
	return address->mailbox;
}

const struct sieve_address_part all_address_part = {
	"all",
	NULL,
	SIEVE_ADDRESS_PART_ALL,
	addrp_all_extract_from
};

const struct sieve_address_part local_address_part = {
	"localpart",
	NULL,
	SIEVE_ADDRESS_PART_LOCAL,
	addrp_localpart_extract_from
};

const struct sieve_address_part domain_address_part = {
	"domain",
	NULL,
	SIEVE_ADDRESS_PART_DOMAIN,
	addrp_domain_extract_from
};

