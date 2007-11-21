#include <stdio.h>

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Forward declarations */

static bool ext_envelope_load(int ext_id);
static bool ext_envelope_validator_load(struct sieve_validator *validator);

static bool ext_envelope_opcode_dump
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);

static bool tst_envelope_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_envelope_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_envelope_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Extension definitions */

static int ext_my_id;

const struct sieve_opcode envelope_opcode =
	{ ext_envelope_opcode_dump, NULL };

const struct sieve_extension envelope_extension = { 
	"envelope", 
	ext_envelope_load,
	ext_envelope_validator_load, 
	NULL, 
	NULL, 
	&envelope_opcode, 
	NULL 
};

static bool ext_envelope_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* Envelope test 
 *
 * Syntax
 *   envelope [COMPARATOR] [ADDRESS-PART] [MATCH-TYPE]
 *     <envelope-part: string-list> <key-list: string-list>   
 */
static const struct sieve_command envelope_test = { 
	"envelope", 
	SCT_TEST, 
	2, 0, FALSE, FALSE,
	tst_envelope_registered, 
	NULL,
	tst_envelope_validate, 
	tst_envelope_generate, 
	NULL 
};

/* Optional arguments */

enum tst_envelope_optional {
	OPT_END,
	OPT_COMPARATOR,
	OPT_ADDRESS_PART,
	OPT_MATCH_TYPE
};

/* Command Registration */
static bool tst_envelope_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, OPT_COMPARATOR);
	sieve_address_parts_link_tags(validator, cmd_reg, OPT_ADDRESS_PART);
	sieve_match_types_link_tags(validator, cmd_reg, OPT_MATCH_TYPE);
	
	return TRUE;
}

/* 
 * Validation 
 */
 
static bool tst_envelope_validate(struct sieve_validator *validator, struct sieve_command_context *tst) 
{ 		
	struct sieve_ast_argument *arg = tst->first_positional;
				
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "envelope part", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, arg);
	
	arg = sieve_ast_argument_next(arg);
	
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, arg);

	/* Validate the key argument to a specified match type */
	sieve_match_type_validate(validator, tst, arg);
	
	return TRUE;
}

/* Load extension into validator */
static bool ext_envelope_validator_load(struct sieve_validator *validator)
{
	/* Register new test */
	sieve_validator_register_command(validator, &envelope_test);

	return TRUE;
}

/*
 * Generation
 */
 
static bool tst_envelope_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	(void)sieve_generator_emit_opcode_ext(generator, ext_my_id);

	/* Generate arguments */
    if ( !sieve_generate_arguments(generator, ctx, NULL) )
        return FALSE;

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool ext_envelope_opcode_dump
	(struct sieve_interpreter *interp, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	unsigned opt_code;

	printf("ENVELOPE\n");

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(sbin, address) ) {
		while ( (opt_code=sieve_operand_optional_read(sbin, address)) ) {
			switch ( opt_code ) {
			case OPT_COMPARATOR:
				sieve_opr_comparator_dump(interp, sbin, address);
				break;
			case OPT_MATCH_TYPE:
				sieve_opr_match_type_dump(interp, sbin, address);
				break;
			case OPT_ADDRESS_PART:
				sieve_opr_address_part_dump(interp, sbin, address);
				break;
			default:
				return FALSE;
			}
		}
	}

	return
		sieve_opr_stringlist_dump(sbin, address) &&
		sieve_opr_stringlist_dump(sbin, address);
}

