/* Extension vacation
 * ------------------
 *
 * Author: Stephan Bosch
 * Specification: draft-ietf-sieve-vacation-07
 * Implementation: validation and generation work, no interpretation/execution.
 * Status: under development
 * 
 */

#include <stdio.h>

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Forward declarations */

static bool ext_vacation_load(int ext_id);
static bool ext_vacation_validator_load(struct sieve_validator *validator);

static bool ext_vacation_opcode_dump
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);

static bool cmd_vacation_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool cmd_vacation_validate(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_vacation_generate(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Extension definitions */

int ext_my_id;

const struct sieve_opcode vacation_opcode = { 
	ext_vacation_opcode_dump, 
	NULL 
};

const struct sieve_extension vacation_extension = { 
	"vacation", 
	ext_vacation_load,
	ext_vacation_validator_load, 
	NULL, 
	NULL, 
	&vacation_opcode, 
	NULL
};

static bool ext_vacation_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

/* Vacation command 
 *	
 * Syntax: 
 *    vacation [":days" number] [":subject" string]
 *                 [":from" string] [":addresses" string-list]
 *                 [":mime"] [":handle" string] <reason: string>
 */
static const struct sieve_command vacation_command = { 
	"vacation",
	SCT_COMMAND, 
	1, 0, FALSE, FALSE, 
	cmd_vacation_registered,
	NULL,  
	cmd_vacation_validate, 
	cmd_vacation_generate, 
	NULL 
};

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

	(*arg)->parameters = sieve_ast_argument_next(*arg);
	
	/* Delete parameter from arg list */
	*arg = sieve_ast_arguments_delete(*arg,1);

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

	(*arg)->parameters = sieve_ast_argument_next(*arg);

	/* Delete parameter from argument list */
	*arg = sieve_ast_arguments_delete(*arg, 1);

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
	
	(*arg)->parameters = sieve_ast_argument_next(*arg);	

	/* Delete parameter from argument list */
	*arg = sieve_ast_arguments_delete(*arg, 1);

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

	(*arg)->parameters = sieve_ast_argument_next(*arg);
	
	/* Delete parameter from argument list */
	*arg = sieve_ast_arguments_delete(*arg, 1);

	return TRUE;
}

/* Command registration */

static const struct sieve_argument vacation_days_tag = { 
	"days", 
	NULL, 
	cmd_vacation_validate_days_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_subject_tag = { 
	"subject", 
	NULL, 
	cmd_vacation_validate_subject_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_from_tag = { 
	"from", 
	NULL, 
	cmd_vacation_validate_from_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_addresses_tag = { 
	"addresses", 
	NULL, 
	cmd_vacation_validate_addresses_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_mime_tag = { 
	"mime",
	NULL, 
	cmd_vacation_validate_mime_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_handle_tag = { 
	"handle", 
	NULL, 
	cmd_vacation_validate_handle_tag, 
	NULL, NULL 
};

enum cmd_vacation_optional {
	OPT_DAYS,
	OPT_SUBJECT,
	OPT_FROM,
	OPT_ADDRESS,
	OPT_MIME,
	OPT_HANDLE
};

static bool cmd_vacation_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag(validator, cmd_reg, &vacation_days_tag, OPT_DAYS); 	
	sieve_validator_register_tag(validator, cmd_reg, &vacation_subject_tag, OPT_SUBJECT); 	
	sieve_validator_register_tag(validator, cmd_reg, &vacation_from_tag, OPT_FROM); 	
	sieve_validator_register_tag(validator, cmd_reg, &vacation_addresses_tag, OPT_ADDRESS); 	
	sieve_validator_register_tag(validator, cmd_reg, &vacation_mime_tag, OPT_MIME); 	
	sieve_validator_register_tag(validator, cmd_reg, &vacation_handle_tag, OPT_HANDLE); 	

	return TRUE;
}

/* Command validation */

static bool cmd_vacation_validate(struct sieve_validator *validator, 
	struct sieve_command_context *cmd) 
{ 	
	struct sieve_ast_argument *arg = cmd->first_positional;

	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "reason", 1, SAAT_STRING) ) {
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
	sieve_generator_emit_opcode_ext(generator, ext_my_id);

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

