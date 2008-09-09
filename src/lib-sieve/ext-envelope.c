/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension envelope 
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC5228
 * Implementation: full
 * Status: experimental, largely untested
 *
 */

#include "lib.h"
#include "str-sanitize.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-address.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

/*
 * Forward declarations
 */

static const struct sieve_command envelope_test;
const struct sieve_operation envelope_operation;
const struct sieve_extension envelope_extension;

/* 
 * Extension 
 */

static int ext_my_id;

static bool ext_envelope_load(int ext_id);
static bool ext_envelope_validator_load(struct sieve_validator *validator);

const struct sieve_extension envelope_extension = { 
	"envelope", 
	&ext_my_id,
	ext_envelope_load,
	ext_envelope_validator_load, 
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_OPERATION(envelope_operation), 
	SIEVE_EXT_DEFINE_NO_OPERANDS 
};

static bool ext_envelope_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

static bool ext_envelope_validator_load(struct sieve_validator *validator)
{
	/* Register new test */
	sieve_validator_register_command(validator, &envelope_test);

	return TRUE;
}

/* 
 * Envelope test 
 *
 * Syntax
 *   envelope [COMPARATOR] [ADDRESS-PART] [MATCH-TYPE]
 *     <envelope-part: string-list> <key-list: string-list>   
 */

static bool tst_envelope_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_envelope_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_envelope_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

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

/* 
 * Envelope operation 
 */

static bool ext_envelope_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int ext_envelope_operation_execute
	(const struct sieve_operation *op,
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation envelope_operation = { 
	"ENVELOPE",
	&envelope_extension,
	0,
	ext_envelope_operation_dump, 
	ext_envelope_operation_execute 
};

/*
 * Envelope parts
 *
 * FIXME: not available to extensions
 */

struct sieve_envelope_part {
	const char *identifier;

	const struct sieve_address *const *(*get_addresses)
		(const struct sieve_runtime_env *renv);
	const char * const *(*get_values)
		(const struct sieve_runtime_env *renv);
};

static const struct sieve_address *const *_from_part_get_addresses
	(const struct sieve_runtime_env *renv);
static const char *const *_from_part_get_values
	(const struct sieve_runtime_env *renv);
static const struct sieve_address *const *_to_part_get_addresses
	(const struct sieve_runtime_env *renv);
static const char *const *_to_part_get_values
	(const struct sieve_runtime_env *renv);
static const char *const *_auth_part_get_values
	(const struct sieve_runtime_env *renv);

static const struct sieve_envelope_part _from_part = {
	"from",
	_from_part_get_addresses,
	_from_part_get_values,
};

static const struct sieve_envelope_part _to_part = {
	"to",
	_to_part_get_addresses,
	_to_part_get_values,
};	

static const struct sieve_envelope_part _auth_part = {
	"auth",
	NULL,
	_auth_part_get_values,
};	

static const struct sieve_envelope_part *_envelope_parts[] = {
	/* Required */
	&_from_part, &_to_part, 

	/* Non-standard */
	&_auth_part
};

static unsigned int _envelope_part_count = N_ELEMENTS(_envelope_parts);

static const struct sieve_envelope_part *_envelope_part_find
(const char *identifier)
{
	unsigned int i;

	for ( i = 0; i < _envelope_part_count; i++ ) {
		if ( strcasecmp( _envelope_parts[i]->identifier, identifier ) == 0 ) {
			return _envelope_parts[i];
        }
	}
	
	return NULL;
}


/* 
 * Command Registration 
 */

static bool tst_envelope_registered
(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
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
 
static int _envelope_part_is_supported
(void *context, struct sieve_ast_argument *arg)
{
	const struct sieve_envelope_part **not_address =
		(const struct sieve_envelope_part **) context;

	if ( sieve_argument_is_string_literal(arg) ) {
		const struct sieve_envelope_part *epart;

		if ( (epart=_envelope_part_find(sieve_ast_strlist_strc(arg))) != NULL ) {
			if ( epart->get_addresses == NULL ) {
				if ( *not_address == NULL )
					*not_address = epart;
			}
					
			return TRUE;
		}
		
		return FALSE;
	} 
	
	return TRUE; /* Can't check at compile time */
}

static bool tst_envelope_validate
(struct sieve_validator *validator, struct sieve_command_context *tst) 
{ 		
	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_ast_argument *epart;
	const struct sieve_envelope_part *not_address = NULL;
				
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "envelope part", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(validator, tst, arg, FALSE) )
		return FALSE;
		
	/* Check whether supplied envelope parts are supported
	 *   FIXME: verify dynamic envelope parts at runtime 
	 */
	epart = arg;
	if ( !sieve_ast_stringlist_map(&epart, (void *) &not_address, 
		_envelope_part_is_supported) ) {		
		
		sieve_command_validate_error(validator, tst, 
			"specified envelope part '%s' is not supported by the envelope test", 
				str_sanitize(sieve_ast_strlist_strc(epart), 64));
		return FALSE;
	}

	if ( not_address != NULL ) {
		struct sieve_ast_argument *addrp_arg = 
			sieve_command_find_argument(tst, &address_part_tag);

		if ( addrp_arg != NULL ) {
			sieve_command_validate_error(validator, tst,
				"address part ':%s' specified while non-address envelope part '%s' "
				"is tested with the envelope test",
                sieve_ast_argument_tag(addrp_arg), not_address->identifier);
	        return FALSE;
		}
	}
	
	arg = sieve_ast_argument_next(arg);
	
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(validator, tst, arg, FALSE) )
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(validator, tst, arg, &is_match_type, &i_ascii_casemap_comparator);
}

/*
 * Code generation
 */
 
static bool tst_envelope_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	(void)sieve_operation_emit_code(cgenv->sbin, &envelope_operation);

	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, ctx, NULL) )
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
		sieve_opr_stringlist_dump(denv, address, "envelope part") &&
		sieve_opr_stringlist_dump(denv, address, "key list");
}

/*
 * Interpretation
 */

static const struct sieve_address *const *_from_part_get_addresses
(const struct sieve_runtime_env *renv)
{
	ARRAY_DEFINE(envelope_values, const struct sieve_address *);
	const struct sieve_address *address =
		sieve_address_parse_envelope_path(renv->msgdata->return_path);
	
	if ( address != NULL ) {
		t_array_init(&envelope_values, 2);

        array_append(&envelope_values, &address, 1);

	    (void)array_append_space(&envelope_values);
    	return array_idx(&envelope_values, 0);
	} 

	return NULL;
}

static const char *const *_from_part_get_values
(const struct sieve_runtime_env *renv)
{
	ARRAY_DEFINE(envelope_values, const char *);

	t_array_init(&envelope_values, 2);

	if ( renv->msgdata->return_path != NULL ) {
        array_append(&envelope_values, &renv->msgdata->return_path, 1);
	}

	(void)array_append_space(&envelope_values);

	return array_idx(&envelope_values, 0);
}

static const struct sieve_address *const *_to_part_get_addresses
(const struct sieve_runtime_env *renv)
{
	ARRAY_DEFINE(envelope_values, const struct sieve_address *);
	const struct sieve_address *address = 
		sieve_address_parse_envelope_path(renv->msgdata->to_address);	

	if ( address != NULL && address->local_part != NULL ) {
		t_array_init(&envelope_values, 2);

        array_append(&envelope_values, &address, 1);

	    (void)array_append_space(&envelope_values);
    	return array_idx(&envelope_values, 0);
	}

	return NULL;
}

static const char *const *_to_part_get_values
(const struct sieve_runtime_env *renv)
{
	ARRAY_DEFINE(envelope_values, const char *);

	t_array_init(&envelope_values, 2);

	if ( renv->msgdata->to_address != NULL ) {
        array_append(&envelope_values, &renv->msgdata->to_address, 1);
	}

	(void)array_append_space(&envelope_values);

	return array_idx(&envelope_values, 0);
}


static const char *const *_auth_part_get_values
(const struct sieve_runtime_env *renv)
{
	ARRAY_DEFINE(envelope_values, const char *);

	t_array_init(&envelope_values, 2);

	if ( renv->msgdata->auth_user != NULL )
        array_append(&envelope_values, &renv->msgdata->auth_user, 1);

	(void)array_append_space(&envelope_values);

	return array_idx(&envelope_values, 0);
}

static int ext_envelope_operation_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool result = TRUE;
	const struct sieve_comparator *cmp = &i_ascii_casemap_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	const struct sieve_address_part *addrp = &all_address_part;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *envp_list;
	struct sieve_coded_stringlist *key_list;
	string_t *envp_item;
	bool matched;
	int ret;

	/*
	 * Read operands
	 */
	
	sieve_runtime_trace(renv, "ENVELOPE test");

	if ( (ret=sieve_addrmatch_default_get_optionals
		(renv, address, &addrp, &mtch, &cmp)) <= 0 )
		return ret; 

	/* Read envelope-part */
	if ( (envp_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid envelope-part operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid key-list operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	/* Initialize match */
	mctx = sieve_match_begin(renv->interp, mtch, cmp, NULL, key_list);
	
	/* Iterate through all requested headers to match */
	envp_item = NULL;
	matched = FALSE;
	while ( result && !matched && 
		(result=sieve_coded_stringlist_next_item(envp_list, &envp_item)) 
		&& envp_item != NULL ) {
		const struct sieve_envelope_part *epart;
			
		if ( (epart=_envelope_part_find(str_c(envp_item))) != NULL ) {
			const struct sieve_address * const *addresses = NULL;
			int i;

			if ( epart->get_addresses != NULL ) {
				/* Field contains addresses */
				addresses = epart->get_addresses(renv);

				if ( addresses != NULL ) {
					for ( i = 0; !matched && addresses[i] != NULL; i++ ) {
						if ( addresses[i]->local_part == NULL ) {
							/* Null path <> */
							ret = sieve_match_value(mctx, "", 0);
						} else {
							const char *part = addrp->extract_from(addresses[i]);
							if ( part != NULL ) 
								ret = sieve_match_value(mctx, part, strlen(part));
						}

						if ( ret < 0 ) {
							result = FALSE;
							break;
						}

       	        		matched = ret > 0;
					}
				}
			} 

			if ( epart->get_values != NULL && addresses == NULL && 
				addrp == &all_address_part ) {
				/* Field contains something else */
				const char *const *values = epart->get_values(renv);

				if ( values == NULL ) continue;
	
				for ( i = 0; !matched && values[i] != NULL; i++ ) {				

					if ( (ret=sieve_match_value
						(mctx, values[i], strlen(values[i]))) < 0 ) {
	                    result = FALSE;
    	                break;
        	        }
			
					matched = ret > 0;				
				}
			}
		}
	}
	
	/* Finish match */
	if ( (ret=sieve_match_end(mctx)) < 0 ) 
		result = FALSE;
	else
		matched = ( ret > 0 || matched );

	if ( result ) {
		/* Set test result for subsequent conditional jump */
		sieve_interpreter_set_test_result(renv->interp, matched);
		return SIEVE_EXEC_OK;
	}
	
	sieve_runtime_trace_error(renv, "invalid string-list item");	
	return SIEVE_EXEC_BIN_CORRUPT;
}

