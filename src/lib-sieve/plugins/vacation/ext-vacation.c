#include <stdio.h>

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Forward declarations */
static bool ext_vacation_validator_load(struct sieve_validator *validator);

static bool ext_vacation_opcode_dump
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);

static bool cmd_vacation_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool cmd_vacation_validate(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_vacation_generate(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Extension definitions */
const struct sieve_opcode vacation_opcode = 
	{ ext_vacation_opcode_dump, NULL };
const struct sieve_extension vacation_extension = 
	{ "vacation", ext_vacation_validator_load, NULL, &vacation_opcode, NULL};
static const struct sieve_command vacation_command = 
	{ "vacation", SCT_COMMAND, cmd_vacation_registered, cmd_vacation_validate, cmd_vacation_generate, NULL };

/* Tag validation */

static bool cmd_vacation_validate_days_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	/* Only one possible tag, so we don't bother checking the identifier */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check syntax:
	 *   :days number
	 */
	if ( (*arg)->type != SAAT_NUMBER ) {
		sieve_command_validate_error(validator, cmd, 
			"the :days tag for the vacation command requires one number argument, but %s was found", sieve_ast_argument_name(*arg) );
		return FALSE;
	}

	/* Skip argument */
	*arg = sieve_ast_argument_next(*arg);

	/* FIXME: assign somewhere */
	
	return TRUE;
}

static bool cmd_vacation_validate_subject_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	/* Only one possible tag, so we don't bother checking the identifier */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check syntax:
	 *   :subject string
	 */
	if ( (*arg)->type != SAAT_STRING ) {
		sieve_command_validate_error(validator, cmd, 
			"the :subject tag for the vacation command requires one string argument, but %s was found", 
				sieve_ast_argument_name(*arg) );
		return FALSE;
	}
	
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);

	/* FIXME: assign somewhere */
	
	return TRUE;
}

static bool cmd_vacation_validate_from_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	/* Only one possible tag, so we don't bother checking the identifier */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check syntax:
	 *   :from string
	 */
	if ( (*arg)->type != SAAT_STRING ) {
		sieve_command_validate_error(validator, cmd, 
			"the :from tag for the vacation command requires one string argument, but %s was found", 
				sieve_ast_argument_name(*arg) );
		return FALSE;
	}

	/* Skip argument */
	*arg = sieve_ast_argument_next(*arg);

	/* FIXME: assign somewhere */
	
	return TRUE;
}

static bool cmd_vacation_validate_addresses_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	/* Only one possible tag, so we don't bother checking the identifier */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check syntax:
	 *   :addresses string-list
	 */
	if ( (*arg)->type != SAAT_STRING && (*arg)->type != SAAT_STRING_LIST ) {
		sieve_command_validate_error(validator, cmd, 
			"the :addresses tag for the vacation command requires one string argument, but %s was found", 
				sieve_ast_argument_name(*arg) );
		return FALSE;
	}
	
	/* FIXME: assign somewhere */
	
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool cmd_vacation_validate_mime_tag
	(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_ast_argument **arg ATTR_UNUSED, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);

	/* FIXME: assign somewhere */
		
	return TRUE;
}

static bool cmd_vacation_validate_handle_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	/* Only one possible tag, so we don't bother checking the identifier */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check syntax:
	 *   :addresses string-list
	 */
	if ( (*arg)->type != SAAT_STRING ) {
		sieve_command_validate_error(validator, cmd, 
			"the :handle tag for the vacation command requires one string argument, but %s was found", 
				sieve_ast_argument_name(*arg) );
		return FALSE;
	}
	
	/* FIXME: assign somewhere */
	
	return TRUE;
}

/* Command registration */

static const struct sieve_argument vacation_days_tag = 
	{ "days", cmd_vacation_validate_days_tag, NULL };
static const struct sieve_argument vacation_subject_tag =
	{ "subject", cmd_vacation_validate_subject_tag, NULL };
static const struct sieve_argument vacation_from_tag = 
	{ "from", cmd_vacation_validate_from_tag, NULL };
static const struct sieve_argument vacation_addresses_tag = 
	{ "addresses", cmd_vacation_validate_addresses_tag, NULL };
static const struct sieve_argument vacation_mime_tag = 
	{ "mime", cmd_vacation_validate_mime_tag, NULL };
static const struct sieve_argument vacation_handle_tag = 
	{ "handle", cmd_vacation_validate_handle_tag, NULL };

static bool cmd_vacation_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag(validator, cmd_reg, &vacation_days_tag); 	
	sieve_validator_register_tag(validator, cmd_reg, &vacation_subject_tag); 	
	sieve_validator_register_tag(validator, cmd_reg, &vacation_from_tag); 	
	sieve_validator_register_tag(validator, cmd_reg, &vacation_addresses_tag); 	
	sieve_validator_register_tag(validator, cmd_reg, &vacation_mime_tag); 	
	sieve_validator_register_tag(validator, cmd_reg, &vacation_handle_tag); 	

	return TRUE;
}

/* Command validation */

static bool cmd_vacation_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg;
	
	/* Check valid syntax: 
	 *    vacation [":days" number] [":subject" string]
	 *                 [":from" string] [":addresses" string-list]
	 *                 [":mime"] [":handle" string] <reason: string>
	 */
	if ( !sieve_validate_command_arguments(validator, cmd, 1, &arg) ||
		!sieve_validate_command_subtests(validator, cmd, 0) || 
	 	!sieve_validate_command_block(validator, cmd, FALSE, FALSE) ) {
	 	
		return FALSE;
	}

	sieve_validator_argument_activate(validator, arg);	
	
	return TRUE;
}

/* Load extension into validator */
static bool ext_vacation_validator_load(struct sieve_validator *validator)
{
	/* Register new command */
	sieve_validator_register_command(validator, &vacation_command);

	return TRUE;
}

/*
 * Generation
 */
 
static bool cmd_vacation_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_opcode_ext(generator, &vacation_extension);

	/* Generate arguments */
    if ( !sieve_generate_arguments(generator, ctx, NULL) )
        return FALSE;	

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool ext_vacation_opcode_dump(
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	printf("VACATION\n");
	sieve_opr_string_dump(sbin, address);
	
	return TRUE;
}

