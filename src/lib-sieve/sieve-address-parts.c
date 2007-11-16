#include <stdio.h>

#include "lib.h"
#include "compat.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"
#include "message-address.h"

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
	(struct sieve_binary *sbin, struct sieve_address_part *addrp);
static void opr_address_part_emit_ext
	(struct sieve_binary *sbin, struct sieve_address_part *addrp, int ext_id);

/* 
 * Address-part 'extension' 
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
	addrp_interpreter_load,
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
 *   name-based address-part registry. 
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
		struct sieve_command_registration *cmd_reg, unsigned int id_code) 
{	
	sieve_validator_register_tag
		(validator, cmd_reg, &address_part_tag, id_code); 	
}

/*
 * Interpreter context:
 */

struct addrp_interpreter_context {
	ARRAY_DEFINE(addrp_extensions, 
		const struct sieve_address_part_extension *); 
};

static inline struct addrp_interpreter_context *
	get_interpreter_context(struct sieve_interpreter *interpreter)
{
	return (struct addrp_interpreter_context *) 
		sieve_interpreter_extension_get_context(interpreter, ext_my_id);
}

const struct sieve_address_part_extension *sieve_address_part_extension_get
	(struct sieve_interpreter *interpreter, int ext_id)
{
	struct addrp_interpreter_context *ctx = get_interpreter_context(interpreter);
	
	if ( (ctx != NULL) && (ext_id > 0) && (ext_id < (int) array_count(&ctx->addrp_extensions)) ) {
		const struct sieve_address_part_extension * const *ext;

		ext = array_idx(&ctx->addrp_extensions, (unsigned int) ext_id);

		return *ext;
	}
	
	return NULL;
}

void sieve_address_part_extension_set
	(struct sieve_interpreter *interpreter, int ext_id,
		const struct sieve_address_part_extension *ext)
{
	struct addrp_interpreter_context *ctx = get_interpreter_context(interpreter);

	array_idx_set(&ctx->addrp_extensions, (unsigned int) ext_id, &ext);
}


static bool addrp_interpreter_load(struct sieve_interpreter *interpreter)
{
	pool_t pool = sieve_interpreter_pool(interpreter);
	
	struct addrp_interpreter_context *ctx = 
		p_new(pool, struct addrp_interpreter_context, 1);
	
	/* Setup comparator registry */
	p_array_init(&ctx->addrp_extensions, pool, 4);

	sieve_interpreter_extension_set_context(interpreter, ext_my_id, ctx);
	
	return TRUE;
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
  
static bool tag_address_part_is_instance_of
	(struct sieve_validator *validator, const char *tag)
{
	return sieve_address_part_find(validator, tag, NULL) != NULL;
}
 
static bool tag_address_part_validate
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	int ext_id;
	const struct sieve_address_part *addrp;

	/* Syntax:   
	 *   ":localpart" / ":domain" / ":all"
   */
	
	/* Get address_part from registry */
	addrp = sieve_address_part_find
		(validator, sieve_ast_argument_tag(*arg), &ext_id);
	
	if ( addrp == NULL ) {
		sieve_command_validate_error(validator, cmd, 
			"unknown address-part modifier '%s' "
			"(this error should not occur and is probably a bug)", 
			sieve_ast_argument_strc(*arg));

		return FALSE;
	}

	/* Store address-part in context */
	(*arg)->context = (void *) addrp;
	(*arg)->ext_id = ext_id;
	
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/* Code generation */

static void opr_address_part_emit
	(struct sieve_binary *sbin, struct sieve_address_part *addrp)
{ 
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_ADDRESS_PART);
	(void) sieve_binary_emit_byte(sbin, addrp->code);
}

static void opr_address_part_emit_ext
	(struct sieve_binary *sbin, struct sieve_address_part *addrp, int ext_id)
{ 
	unsigned char addrp_code = SIEVE_ADDRESS_PART_CUSTOM + 
		sieve_binary_extension_get_index(sbin, ext_id);
	
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_ADDRESS_PART);	
	(void) sieve_binary_emit_byte(sbin, addrp_code);
	(void) sieve_binary_emit_byte(sbin, addrp->ext_code);
}

const struct sieve_address_part *sieve_opr_address_part_read
  (struct sieve_interpreter *interpreter, 
		struct sieve_binary *sbin, sieve_size_t *address)
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
			int ext_id = -1;
			const struct sieve_address_part_extension *ap_ext;

			if ( sieve_binary_extension_get_by_index(sbin,
				addrp_code - SIEVE_ADDRESS_PART_CUSTOM, &ext_id) == NULL )
				return NULL; 

			ap_ext = sieve_address_part_extension_get(interpreter, ext_id); 
 
			if ( ap_ext != NULL ) {  	
				unsigned int code;
				if ( ap_ext->address_part != NULL )
					return ap_ext->address_part;
		  	
				if ( sieve_binary_read_byte(sbin, address, &code) &&
					ap_ext->get_part != NULL )
				return ap_ext->get_part(code);
			} else {
				i_info("Unknown address-part modifier %d.", addrp_code); 
			}
		}
	}		
		
	return NULL; 
}

bool sieve_opr_address_part_dump
	(struct sieve_interpreter *interpreter,
		struct sieve_binary *sbin, sieve_size_t *address)
{
	sieve_size_t pc = *address;
	const struct sieve_address_part *addrp = 
		sieve_opr_address_part_read(interpreter, sbin, address);
	
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
			opr_address_part_emit(sbin, addrp);
		else
			return FALSE;
	} else {
		opr_address_part_emit_ext(sbin, addrp, (*arg)->ext_id);
	} 
		
	*arg = sieve_ast_argument_next(*arg);
	
	return TRUE;
}

/*
 * Address Matching
 */
 
bool sieve_address_stringlist_match
	(const struct sieve_address_part *addrp, 
		struct sieve_coded_stringlist *key_list,
		const struct sieve_comparator *cmp,	const char *data)
{
	bool matched = FALSE;
	const struct message_address *addr;
	
	t_push();
	
	addr = message_address_parse
		(unsafe_data_stack_pool, (const unsigned char *) data, 
			strlen(data), 256, FALSE);
	
	while (!matched && addr != NULL) {
		if (addr->domain != NULL) {
			/* mailbox@domain */
			const char *part;
			
			i_assert(addr->mailbox != NULL);

			part = addrp->extract_from(addr);
			
			if ( sieve_stringlist_match(key_list, part, cmp) )
				matched = TRUE;				
		} 

		addr = addr->next;
	}
	
	t_pop();
	
	return matched;
}

/* 
 * Core address-part modifiers
 */

const struct sieve_argument address_part_tag = { 
	NULL,
	tag_address_part_is_instance_of, 
	tag_address_part_validate, 
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
	SIEVE_ADDRESS_PART_ALL,
	NULL,
	0,
	addrp_all_extract_from
};

const struct sieve_address_part local_address_part = {
	"localpart",
	SIEVE_ADDRESS_PART_LOCAL,
	NULL,
	0,
	addrp_localpart_extract_from
};

const struct sieve_address_part domain_address_part = {
	"domain",
	SIEVE_ADDRESS_PART_DOMAIN,
	NULL,
	0,
	addrp_domain_extract_from
};

const struct sieve_address_part *sieve_core_address_parts[] = {
	&all_address_part, &local_address_part, &domain_address_part
};

const unsigned int sieve_core_address_parts_count = 
	N_ELEMENTS(sieve_core_address_parts);


