/* Extension vacation
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-ietf-sieve-vacation-07
 * Implementation: validation, generation and interpretation, no actual 
 *   execution.
 * Status: experimental, largely untested
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
	(const struct sieve_opcode *opcode,	
		const struct sieve_runtime_env *renv, sieve_size_t *address);
static bool ext_vacation_opcode_execute
	(const struct sieve_opcode *opcode, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_vacation_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool cmd_vacation_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_vacation_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Extension definitions */

int ext_my_id;

const struct sieve_opcode vacation_opcode;

const struct sieve_extension vacation_extension = { 
	"vacation", 
	ext_vacation_load,
	ext_vacation_validator_load, 
	NULL, 
	NULL, 
	SIEVE_EXT_DEFINE_OPCODE(vacation_opcode),
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

/* Vacation opcode */
const struct sieve_opcode vacation_opcode = { 
	"VACATION",
	SIEVE_OPCODE_CUSTOM,
	&vacation_extension,
	0,
	ext_vacation_opcode_dump, 
	ext_vacation_opcode_execute
};

/* Tag validation */

static bool cmd_vacation_validate_number_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	
	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);
	
	/* Check syntax:
	 *   :days number
	 */
	if ( !sieve_validate_tag_parameter
		(validator, cmd, tag, *arg, SAAT_NUMBER) ) {
		return FALSE;
	}

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);
	
	return TRUE;
}

static bool cmd_vacation_validate_string_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);
	
	/* Check syntax:
	 *   :subject string
	 *   :from string
	 *   :handle string
	 */
	if ( !sieve_validate_tag_parameter
		(validator, cmd, tag, *arg, SAAT_STRING) ) {
		return FALSE;
	}
		
	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool cmd_vacation_validate_stringlist_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);
	
	/* Check syntax:
	 *   :addresses string-list
	 */
	if ( !sieve_validate_tag_parameter
		(validator, cmd, tag, *arg, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool cmd_vacation_validate_mime_tag
	(struct sieve_validator *validator ATTR_UNUSED, 
	struct sieve_ast_argument **arg ATTR_UNUSED, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	/* FIXME: currently not generated */
	*arg = sieve_ast_arguments_detach(*arg,1);
		
	return TRUE;
}

/* Command registration */

static const struct sieve_argument vacation_days_tag = { 
	"days", NULL, 
	cmd_vacation_validate_number_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_subject_tag = { 
	"subject", NULL, 
	cmd_vacation_validate_string_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_from_tag = { 
	"from", NULL, 
	cmd_vacation_validate_string_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_addresses_tag = { 
	"addresses", NULL, 
	cmd_vacation_validate_stringlist_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_mime_tag = { 
	"mime",	NULL, 
	cmd_vacation_validate_mime_tag, 
	NULL, NULL 
};

static const struct sieve_argument vacation_handle_tag = { 
	"handle", NULL, 
	cmd_vacation_validate_string_tag, 
	NULL, NULL 
};

enum cmd_vacation_optional {
	OPT_END,
	OPT_DAYS,
	OPT_SUBJECT,
	OPT_FROM,
	OPT_ADDRESSES,
	OPT_MIME,
	OPT_HANDLE
};

static bool cmd_vacation_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_days_tag, OPT_DAYS); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_subject_tag, OPT_SUBJECT); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_from_tag, OPT_FROM); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_addresses_tag, OPT_ADDRESSES); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_mime_tag, OPT_MIME); 	
	sieve_validator_register_tag
		(validator, cmd_reg, &vacation_handle_tag, OPT_HANDLE); 	

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
	sieve_generator_emit_opcode_ext(generator, &vacation_opcode, ext_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool ext_vacation_opcode_dump
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	unsigned int opt_code;
	
	printf("VACATION\n");
	
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( (opt_code=sieve_operand_optional_read(renv->sbin, address)) ) {
			switch ( opt_code ) {
			case OPT_DAYS:
				if ( !sieve_opr_number_dump(renv->sbin, address) )
					return FALSE;
				break;
			case OPT_SUBJECT:
			case OPT_FROM:
			case OPT_HANDLE: 
				if ( !sieve_opr_string_dump(renv->sbin, address) )
					return FALSE;
				break;
			case OPT_ADDRESSES:
				if ( !sieve_opr_stringlist_dump(renv->sbin, address) )
					return FALSE;
				break;
			case OPT_MIME:
				break;
			
			default:
				return FALSE;
			}
		}
	}
	
	return sieve_opr_string_dump(renv->sbin, address);
}

/* 
 * Code execution
 */
 
static bool ext_vacation_opcode_execute
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	unsigned int opt_code;
	sieve_size_t days = 0;
	string_t *reason, *subject, *from, *handle;
		
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( (opt_code=sieve_operand_optional_read(renv->sbin, address)) ) {
			switch ( opt_code ) {
			case OPT_DAYS:
				if ( !sieve_opr_number_read(renv->sbin, address, &days) ) return FALSE;
				break;
			case OPT_SUBJECT:
				if ( !sieve_opr_string_read(renv->sbin, address, &subject) ) return FALSE;
				break;
			case OPT_FROM:
				if ( !sieve_opr_string_read(renv->sbin, address, &from) ) return FALSE;
				break;
			case OPT_HANDLE: 
				if ( !sieve_opr_string_read(renv->sbin, address, &handle) ) return FALSE;
				break;
			case OPT_ADDRESSES:
				if ( sieve_opr_stringlist_read(renv->sbin, address) == NULL ) return FALSE;
				break;
			case OPT_MIME:
				break;
			default:
				return FALSE;
			}
		}
	}
	
	if ( !sieve_opr_string_read(renv->sbin, address, &reason) ) 
		return FALSE;
	
	printf(">> VACATION \"%s\"\n", str_c(reason));
	
	return TRUE;
}


