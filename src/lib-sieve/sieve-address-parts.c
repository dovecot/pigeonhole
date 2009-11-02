/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"
#include "compat.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"
#include "message-address.h"

#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-address.h"
#include "sieve-commands.h"
#include "sieve-binary.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

#include "sieve-address-parts.h"

#include <string.h>

/* 
 * Default address parts
 */

const struct sieve_address_part_def *sieve_core_address_parts[] = {
	&all_address_part, &local_address_part, &domain_address_part
};

const unsigned int sieve_core_address_parts_count = 
	N_ELEMENTS(sieve_core_address_parts);

/* 
 * Address-part 'extension' 
 */

static bool addrp_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);

const struct sieve_extension_def address_part_extension = {
	"@address-parts",
	NULL, NULL,
	addrp_validator_load,
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS,
	SIEVE_EXT_DEFINE_NO_OPERANDS /* Defined as core operand */
};
	
/* 
 * Validator context:
 *   name-based address-part registry. 
 */

static struct sieve_validator_object_registry *_get_object_registry
(struct sieve_validator *valdtr)
{
	struct sieve_instance *svinst;
	const struct sieve_extension *adrp_ext;

	svinst = sieve_validator_svinst(valdtr);
	adrp_ext = sieve_get_address_part_extension(svinst);
	return sieve_validator_object_registry_get(valdtr, adrp_ext);
}
 
void sieve_address_part_register
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	const struct sieve_address_part_def *addrp_def) 
{
	struct sieve_validator_object_registry *regs = _get_object_registry(valdtr);
	
	sieve_validator_object_registry_add(regs, ext, &addrp_def->obj_def);
}

static bool sieve_address_part_exists
(struct sieve_validator *valdtr, const char *identifier) 
{
	struct sieve_validator_object_registry *regs = _get_object_registry(valdtr);

	return sieve_validator_object_registry_find(regs, identifier, NULL);
}

static const struct sieve_address_part *sieve_address_part_create_instance
(struct sieve_validator *valdtr, struct sieve_command *cmd, 
	const char *identifier) 
{
	struct sieve_validator_object_registry *regs = _get_object_registry(valdtr);
	struct sieve_object object;
	struct sieve_address_part *addrp;

	if ( !sieve_validator_object_registry_find(regs, identifier, &object) )
		return NULL;

	addrp = p_new(sieve_command_pool(cmd), struct sieve_address_part, 1);
	addrp->object = object;
	addrp->def = (const struct sieve_address_part_def *) object.def;

  return addrp;
}

static bool addrp_validator_load
(const struct sieve_extension *ext, struct sieve_validator *valdtr)
{
	struct sieve_validator_object_registry *regs = 
		sieve_validator_object_registry_init(valdtr, ext);
	unsigned int i;

	/* Register core address-parts */
	for ( i = 0; i < sieve_core_address_parts_count; i++ ) {
		sieve_validator_object_registry_add
			(regs, NULL, &(sieve_core_address_parts[i]->obj_def));
	}

	return TRUE;
}

void sieve_address_parts_link_tags
(struct sieve_validator *valdtr, struct sieve_command_registration *cmd_reg,
	int id_code) 
{	
	struct sieve_instance *svinst;
	const struct sieve_extension *adrp_ext;

	svinst = sieve_validator_svinst(valdtr);
	adrp_ext = sieve_get_address_part_extension(svinst);

	sieve_validator_register_tag
		(valdtr, cmd_reg, adrp_ext, &address_part_tag, id_code); 	
}

/* 
 * Address-part tagged argument 
 */
 
/* Forward declarations */

static bool tag_address_part_is_instance_of
	(struct sieve_validator *valdtr, struct sieve_command *cmd,
		const struct sieve_extension *ext, const char *identifier, void **data);
static bool tag_address_part_validate
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg, 
		struct sieve_command *cmd);
static bool tag_address_part_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command *cmd);

/* Argument object */

const struct sieve_argument_def address_part_tag = { 
	"ADDRESS-PART",
	tag_address_part_is_instance_of, 
	tag_address_part_validate,
	NULL, NULL,
	tag_address_part_generate 
};

/* Argument implementation */
  
static bool tag_address_part_is_instance_of
(struct sieve_validator *valdtr, struct sieve_command *cmd,
	const struct sieve_extension *ext ATTR_UNUSED, const char *identifier, 
	void **data)
{
	const struct sieve_address_part *addrp;

	if ( data == NULL )
		return sieve_address_part_exists(valdtr, identifier);

	if ( (addrp=sieve_address_part_create_instance
		(valdtr, cmd, identifier)) == NULL )
		return FALSE;
	
	*data = (void *) addrp;
	return TRUE;
}
 
static bool tag_address_part_validate
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_ast_argument **arg, 
	struct sieve_command *cmd ATTR_UNUSED)
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

static bool tag_address_part_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command *cmd ATTR_UNUSED)
{
	struct sieve_address_part *addrp =
		(struct sieve_address_part *) arg->argument->data;
		
	sieve_opr_address_part_emit(cgenv->sbin, addrp); 
		
	return TRUE;
}

/*
 * Address-part operand
 */
 
const struct sieve_operand_class sieve_address_part_operand_class = 
	{ "address part" };

static const struct sieve_extension_objects core_address_parts =
	SIEVE_EXT_DEFINE_MATCH_TYPES(sieve_core_address_parts);

const struct sieve_operand_def address_part_operand = { 
	"address-part", 
	NULL, SIEVE_OPERAND_ADDRESS_PART,
	&sieve_address_part_operand_class,
	&core_address_parts
};

/*
 * Address Matching
 */
 
int sieve_address_match
(const struct sieve_address_part *addrp, struct sieve_match_context *mctx, 		
	const char *data)
{
	int result = FALSE;
	const struct message_address *addr;

	T_BEGIN {
		bool valid = TRUE;
		const struct message_address *aitem;

		addr = message_address_parse
			(pool_datastack_create(), (const unsigned char *) data, 
				strlen(data), 256, FALSE);

		/* Check validity of all addresses simultaneously. Unfortunately,
		 * errorneous addresses cannot be extracted from the address list
		 * and therefore :all will match against the whole header value
		 * which is not entirely standard.
		 */
		aitem = addr;
		while ( aitem != NULL) {
			if ( aitem->invalid_syntax )
				valid = FALSE;
			aitem = aitem->next;
		}

		if ( !valid || addr == NULL ) {
			if ( sieve_address_part_is(addrp, all_address_part) )
				result = sieve_match_value(mctx, data, strlen(data));
			else 
				result = FALSE;
		} else {
			while ( result == 0 && addr != NULL) {
				/* mailbox@domain */
				struct sieve_address address;
				const char *part = NULL;
			
				if ( addr->domain != NULL ) {
					address.local_part = addr->mailbox;
					address.domain = addr->domain;
	
					if ( addrp->def != NULL && addrp->def->extract_from ) 
						part = addrp->def->extract_from(addrp, &address);

					if ( part != NULL )
						result = sieve_match_value(mctx, part, strlen(part));
				}
				addr = addr->next;
			}
		}
	} T_END;
	
	return result;
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
	struct sieve_address_part *addrp, struct sieve_match_type *mtch, 
	struct sieve_comparator *cmp) 
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
				if ( !sieve_opr_comparator_read(renv, address, cmp) )
					return FALSE;
				break;
			case SIEVE_AM_OPT_MATCH_TYPE:
				if ( !sieve_opr_match_type_read(renv, address, mtch) )
					return FALSE;
				break;
			case SIEVE_AM_OPT_ADDRESS_PART:
				if ( !sieve_opr_address_part_read(renv, address, addrp) )
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
 
static const char *addrp_all_extract_from
(const struct sieve_address_part *addrp ATTR_UNUSED, 
	const struct sieve_address *address)
{
	const char *local_part = address->local_part;
	const char *domain = address->domain;

	return t_strconcat(local_part, "@", domain, NULL);
}

static const char *addrp_domain_extract_from
(const struct sieve_address_part *addrp ATTR_UNUSED,
	const struct sieve_address *address)
{
	return address->domain;
}

static const char *addrp_localpart_extract_from
(const struct sieve_address_part *addrp ATTR_UNUSED,
	const struct sieve_address *address)
{
	return address->local_part;
}

const struct sieve_address_part_def all_address_part = {
	SIEVE_OBJECT("all", &address_part_operand, SIEVE_ADDRESS_PART_ALL),
	addrp_all_extract_from
};

const struct sieve_address_part_def local_address_part = {
	SIEVE_OBJECT("localpart", &address_part_operand, SIEVE_ADDRESS_PART_LOCAL),
	addrp_localpart_extract_from
};

const struct sieve_address_part_def domain_address_part = {
	SIEVE_OBJECT("domain", &address_part_operand,	SIEVE_ADDRESS_PART_DOMAIN),
	addrp_domain_extract_from
};

