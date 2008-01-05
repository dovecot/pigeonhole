/* Extension envelope 
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC3028, draft-ietf-sieve-3028bis-13.txt
 * Implementation: full
 * Status: experimental, largely untested
 *
 */

#include <stdio.h>

#include "lib.h"
#include "array.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

/* Forward declarations */

static bool ext_envelope_load(int ext_id);
static bool ext_envelope_validator_load(struct sieve_validator *validator);

static bool ext_envelope_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool ext_envelope_operation_execute
	(const struct sieve_operation *op,
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool tst_envelope_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_envelope_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_envelope_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Extension definitions */

static int ext_my_id;

const struct sieve_operation envelope_operation;

const struct sieve_extension envelope_extension = { 
	"envelope", 
	ext_envelope_load,
	ext_envelope_validator_load, 
	NULL, NULL, NULL, 
	SIEVE_EXT_DEFINE_OPERATION(envelope_operation), 
	SIEVE_EXT_DEFINE_NO_OPERANDS 
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

/* Envelope operation */

const struct sieve_operation envelope_operation = { 
	"ENVELOPE",
	&envelope_extension,
	0,
	ext_envelope_operation_dump, 
	ext_envelope_operation_execute 
};

/* Command Registration */
static bool tst_envelope_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, SIEVE_AM_OPT_COMPARATOR);
	sieve_address_parts_link_tags(validator, cmd_reg, SIEVE_AM_OPT_ADDRESS_PART);
	sieve_match_types_link_tags(validator, cmd_reg, SIEVE_AM_OPT_MATCH_TYPE);
	
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
	sieve_validator_argument_activate(validator, tst, arg, FALSE);
	
	arg = sieve_ast_argument_next(arg);
	
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, tst, arg, FALSE);

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
	(void)sieve_generator_emit_operation_ext
		(generator, &envelope_operation, ext_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;

	return TRUE;
}

/* 
 * Code dump 
 */
 
static bool ext_envelope_operation_dump
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "ENVELOPE");
	sieve_code_descend(denv);

	/* Handle any optional arguments */
	if ( !sieve_addrmatch_default_dump_optionals(denv, address) )
		return FALSE;

	return
		sieve_opr_stringlist_dump(denv, address) &&
		sieve_opr_stringlist_dump(denv, address);
}

static int ext_envelope_get_fields
(const struct sieve_message_data *msgdata, const char *field, 
	const char *const **value_r) 
{
	const char *value;
	ARRAY_DEFINE(envelope_values, const char *);
	
 	p_array_init(&envelope_values, pool_datastack_create(), 2);
 	
	if ( strncmp(field, "from", 4) == 0 )
		value = msgdata->return_path;
	else if ( strncmp(field, "to", 2) == 0 )
		value = msgdata->to_address;	
	else if ( strncmp(field, "auth", 2) == 0 ) /* Non-standard */
		value = msgdata->auth_user;
		
	if ( value != NULL )
		array_append(&envelope_values, &value, 1);
	
	(void)array_append_space(&envelope_values);
	*value_r = array_idx(&envelope_values, 0);

	return 0;
}

static bool ext_envelope_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool result = TRUE;
	const struct sieve_comparator *cmp = &i_octet_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	const struct sieve_address_part *addrp = &all_address_part;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *hdr_list;
	struct sieve_coded_stringlist *key_list;
	string_t *hdr_item;
	bool matched;
	
	printf("?? ENVELOPE\n");

	if ( !sieve_addrmatch_default_get_optionals
		(renv, address, &addrp, &mtch, &cmp) )
		return FALSE; 

	t_push();
		
	/* Read header-list */
	if ( (hdr_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		t_pop();
		return FALSE;
	}

	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		t_pop();
		return FALSE;
	}
	
	/* Initialize match context */
	mctx = sieve_match_begin(mtch, cmp, key_list);
	
	/* Iterate through all requested headers to match */
	hdr_item = NULL;
	matched = FALSE;
	while ( !matched && (result=sieve_coded_stringlist_next_item(hdr_list, &hdr_item)) 
		&& hdr_item != NULL ) {
		const char *const *fields;
			
		if ( ext_envelope_get_fields(renv->msgdata, str_c(hdr_item), &fields) >= 0 ) {	
			
			int i;
			for ( i = 0; !matched && fields[i] != NULL; i++ ) {
				if ( sieve_address_match(addrp, mctx, fields[i]) )
					matched = TRUE;				
			} 
		}
	}
	
	matched = sieve_match_end(mctx) || matched;

	t_pop();
	
	if ( result )
		sieve_interpreter_set_test_result(renv->interp, matched);
	
	return result;
}
