#include <stdio.h>

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
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

static const struct sieve_command envelope_test = 
	{ "envelope", SCT_TEST, tst_envelope_registered, tst_envelope_validate, tst_envelope_generate, NULL };

static bool ext_envelope_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

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
	sieve_validator_link_match_type_tags(validator, cmd_reg, OPT_MATCH_TYPE);
	
	return TRUE;
}

/* 
 * Validation 
 */
 
static bool tst_envelope_validate(struct sieve_validator *validator, struct sieve_command_context *tst) 
{ 		
	struct sieve_ast_argument *arg;
	
	/* Check envelope test syntax (optional tags are registered above):
	 *   envelope [COMPARATOR] [ADDRESS-PART] [MATCH-TYPE]
	 *     <envelope-part: string-list> <key-list: string-list>   
	 */
	if ( !sieve_validate_command_arguments(validator, tst, 2, &arg) ||
		!sieve_validate_command_subtests(validator, tst, 0) ) {
		return FALSE;
	}
				
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) {
		sieve_command_validate_error(validator, tst, 
			"the envelope test expects a string-list as first argument (envelope part), but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE; 
	}
	sieve_validator_argument_activate(validator, arg);
	
	arg = sieve_ast_argument_next(arg);
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) {
		sieve_command_validate_error(validator, tst, 
			"the envelope test expects a string-list as second argument (key list), but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE; 
	}
	sieve_validator_argument_activate(validator, arg);
	
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
	(struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	unsigned opt_code;

	printf("ENVELOPE\n");

	/* Handle any optional arguments */
    if ( sieve_operand_optional_present(sbin, address) ) {
        while ( (opt_code=sieve_operand_optional_read(sbin, address)) ) {
            switch ( opt_code ) {
            case OPT_COMPARATOR:
                sieve_opr_comparator_dump(sbin, address);
                break;
            case OPT_MATCH_TYPE:
                break;
			case OPT_ADDRESS_PART:
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

