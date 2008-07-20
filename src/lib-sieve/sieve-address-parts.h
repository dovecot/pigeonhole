#ifndef __SIEVE_ADDRESS_PARTS_H
#define __SIEVE_ADDRESS_PARTS_H

#include "message-address.h"

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-objects.h"

/*
 * Address part object 
 */

struct sieve_address_part {
	struct sieve_object object;		

	const char *(*extract_from)(const struct message_address *address);
};

/*
 * Core address parts
 */
 
enum sieve_address_part_code {
	SIEVE_ADDRESS_PART_ALL,
	SIEVE_ADDRESS_PART_LOCAL,
	SIEVE_ADDRESS_PART_DOMAIN,
	SIEVE_ADDRESS_PART_CUSTOM
};

extern const struct sieve_address_part all_address_part;
extern const struct sieve_address_part local_address_part;
extern const struct sieve_address_part domain_address_part;

/*
 * Address part tagged argument
 */
 
extern const struct sieve_argument address_part_tag;

struct sieve_address_part_context {
	struct sieve_command_context *command_ctx;
	const struct sieve_address_part *address_part;
	
	int ext_id;
};

void sieve_address_parts_link_tags
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg, int id_code);

/*
 * Address part registry
 */
		
void sieve_address_part_register
	(struct sieve_validator *validator, 
		const struct sieve_address_part *addrp, int ext_id);
const struct sieve_address_part *sieve_address_part_find
	(struct sieve_validator *validator, const char *identifier,
		int *ext_id);
		
/*
 * Address part operand
 */

const struct sieve_operand address_part_operand;
struct sieve_operand_class sieve_address_part_operand_class;

#define SIEVE_EXT_DEFINE_ADDRESS_PART(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_ADDRESS_PARTS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

static inline void sieve_opr_address_part_emit
(struct sieve_binary *sbin, const struct sieve_address_part *addrp, int ext_id)
{ 
	sieve_opr_object_emit(sbin, &addrp->object, ext_id);
}

static inline const struct sieve_address_part *sieve_opr_address_part_read
  (const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	return (const struct sieve_address_part *) sieve_opr_object_read
		(renv, &sieve_address_part_operand_class, address);
}

static inline bool sieve_opr_address_part_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	return sieve_opr_object_dump
		(denv, &sieve_address_part_operand_class, address, NULL);
}

/* Match utility */

bool sieve_address_match
(const struct sieve_address_part *addrp, struct sieve_match_context *mctx,
    const char *data);

enum sieve_addrmatch_opt_operand {
	SIEVE_AM_OPT_END,
	SIEVE_AM_OPT_COMPARATOR,
	SIEVE_AM_OPT_ADDRESS_PART,
	SIEVE_AM_OPT_MATCH_TYPE
};

bool sieve_addrmatch_default_dump_optionals
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
bool sieve_addrmatch_default_get_optionals
	(const struct sieve_runtime_env *renv, sieve_size_t *address, 
		const struct sieve_address_part **addp, 
		const struct sieve_match_type **mtch, const struct sieve_comparator **cmp);

#endif /* __SIEVE_ADDRESS_PARTS_H */
