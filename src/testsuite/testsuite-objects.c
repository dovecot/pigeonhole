/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "string.h"
#include "ostream.h"
#include "hash.h"
#include "mail-storage.h"

#include "mail-raw.h"
#include "namespaces.h"
#include "sieve.h"
#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-extensions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "testsuite-common.h"
#include "testsuite-objects.h"
 
/* 
 * Testsuite core objects
 */
 
enum testsuite_object_code {
	TESTSUITE_OBJECT_MESSAGE,
	TESTSUITE_OBJECT_ENVELOPE
};

const struct testsuite_object *testsuite_core_objects[] = {
	&message_testsuite_object, &envelope_testsuite_object
};

const unsigned int testsuite_core_objects_count =
	N_ELEMENTS(testsuite_core_objects);

/* 
 * Testsuite object registry
 */
 
void testsuite_object_register
(struct sieve_validator *valdtr, const struct testsuite_object *tobj) 
{
	struct testsuite_validator_context *ctx = testsuite_validator_context_get
		(valdtr);
	
	sieve_validator_object_registry_add
		(ctx->object_registrations, &tobj->object);
}

const struct testsuite_object *testsuite_object_find
(struct sieve_validator *valdtr, const char *identifier) 
{
	struct testsuite_validator_context *ctx = testsuite_validator_context_get
		(valdtr);
	const struct sieve_object *object = 
		sieve_validator_object_registry_find
			(ctx->object_registrations, identifier);

	return (const struct testsuite_object *) object;
}

void testsuite_register_core_objects
	(struct testsuite_validator_context *ctx)
{
	unsigned int i;
	
	/* Register core testsuite objects */
	for ( i = 0; i < testsuite_core_objects_count; i++ ) {
		sieve_validator_object_registry_add
			(ctx->object_registrations, &(testsuite_core_objects[i]->object));
	}
}
 
/* 
 * Testsuite object code
 */ 
 
const struct sieve_operand_class sieve_testsuite_object_operand_class = 
	{ "testsuite object" };

static const struct sieve_extension_obj_registry core_testsuite_objects =
	SIEVE_EXT_DEFINE_OBJECTS(testsuite_core_objects);

const struct sieve_operand testsuite_object_operand = { 
	"testsuite-object",
	&testsuite_extension, 
	TESTSUITE_OPERAND_OBJECT, 
	&sieve_testsuite_object_operand_class,
	&core_testsuite_objects
};

static void testsuite_object_emit
(struct sieve_binary *sbin, const struct testsuite_object *obj,
	int member_id)
{ 
	sieve_opr_object_emit(sbin, &obj->object);
	
	if ( obj->get_member_id != NULL ) {
		(void) sieve_binary_emit_byte(sbin, (unsigned char) member_id);
	}
}

const struct testsuite_object *testsuite_object_read
(struct sieve_binary *sbin, sieve_size_t *address)
{
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	
	return (const struct testsuite_object *) sieve_opr_object_read_data
		(sbin, operand, &sieve_testsuite_object_operand_class, address);
}

const struct testsuite_object *testsuite_object_read_member
(struct sieve_binary *sbin, sieve_size_t *address, int *member_id)
{
	const struct testsuite_object *object;
		
	if ( (object = testsuite_object_read(sbin, address)) == NULL )
		return NULL;
		
	*member_id = -1;
	if ( object->get_member_id != NULL ) {
		if ( !sieve_binary_read_code(sbin, address, member_id) ) 
			return NULL;
	}
	
	return object;
}

const char *testsuite_object_member_name
(const struct testsuite_object *object, int member_id)
{
	const char *member = NULL;

	if ( object->get_member_id != NULL ) {
		if ( object->get_member_name != NULL )
			member = object->get_member_name(member_id);
	} else 
		return object->object.identifier;
		
	if ( member == NULL )	
		return t_strdup_printf("%s.%d", object->object.identifier, member_id);
	
	return t_strdup_printf("%s.%s", object->object.identifier, member);
}

bool testsuite_object_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct testsuite_object *object;
	int member_id;

	sieve_code_mark(denv);
		
	if ( (object = testsuite_object_read_member(denv->sbin, address, &member_id)) 
		== NULL )
		return FALSE;
	
	sieve_code_dumpf(denv, "%s: %s",
		sieve_testsuite_object_operand_class.name, 
		testsuite_object_member_name(object, member_id));
	
	return TRUE;
}

/* 
 * Testsuite object argument
 */
 
static bool arg_testsuite_object_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command_context *cmd);

const struct sieve_argument testsuite_object_argument = { 
	"testsuite-object", 
	NULL, NULL, NULL, NULL,
	arg_testsuite_object_generate 
};
 
struct testsuite_object_argctx {
	const struct testsuite_object *object;
	int member;
};

bool testsuite_object_argument_activate
(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
	struct sieve_command_context *cmd) 
{
	const char *objname = sieve_ast_argument_strc(arg);
	const struct testsuite_object *object;
	int member_id;
	const char *member;
	struct testsuite_object_argctx *ctx;
	
	/* Parse the object specifier */
	
	member = strchr(objname, '.');
	if ( member != NULL ) {
		objname = t_strdup_until(objname, member);
		member++;
	}
	
	/* Find the object */
	
	object = testsuite_object_find(valdtr, objname);
	if ( object == NULL ) {
		sieve_command_validate_error(valdtr, cmd, 
			"unknown testsuite object '%s'", objname);
		return FALSE;
	}
	
	/* Find the object member */
	
	member_id = -1;
	if ( member != NULL ) {
		if ( object->get_member_id == NULL || 
			(member_id=object->get_member_id(member)) == -1 ) {
			sieve_command_validate_error(valdtr, cmd, 
				"member '%s' does not exist for testsuite object '%s'", member, objname);
			return FALSE;
		}
	}
	
	/* Assign argument context */
	
	ctx = p_new(sieve_command_pool(cmd), struct testsuite_object_argctx, 1);
	ctx->object = object;
	ctx->member = member_id;
	
	arg->argument = &testsuite_object_argument;
	arg->context = (void *) ctx;
	
	return TRUE;
}

static bool arg_testsuite_object_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct testsuite_object_argctx *ctx = 
		(struct testsuite_object_argctx *) arg->context;
	
	testsuite_object_emit(cgenv->sbin, ctx->object, ctx->member);
		
	return TRUE;
}

/* 
 * Testsuite core object implementation
 */
 
static bool tsto_message_set_member
	(const struct sieve_runtime_env *renv, int id, string_t *value);

static int tsto_envelope_get_member_id(const char *identifier);
static const char *tsto_envelope_get_member_name(int id);
static bool tsto_envelope_set_member
	(const struct sieve_runtime_env *renv, int id, string_t *value);

const struct testsuite_object message_testsuite_object = { 
	SIEVE_OBJECT("message",	&testsuite_object_operand, TESTSUITE_OBJECT_MESSAGE),
	NULL, NULL, 
	tsto_message_set_member, 
	NULL
};

const struct testsuite_object envelope_testsuite_object = { 
	SIEVE_OBJECT("envelope", &testsuite_object_operand, TESTSUITE_OBJECT_ENVELOPE),
	tsto_envelope_get_member_id, 
	tsto_envelope_get_member_name,
	tsto_envelope_set_member, 
	NULL
};

enum testsuite_object_envelope_field {
	TESTSUITE_OBJECT_ENVELOPE_FROM,
	TESTSUITE_OBJECT_ENVELOPE_TO,
	TESTSUITE_OBJECT_ENVELOPE_AUTH_USER
};

static bool tsto_message_set_member
(const struct sieve_runtime_env *renv, int id, string_t *value) 
{
	if ( id != -1 ) return FALSE;
	
	testsuite_message_set(renv, value);
	
	return TRUE;
}

static int tsto_envelope_get_member_id(const char *identifier)
{
	if ( strcasecmp(identifier, "from") == 0 )
		return TESTSUITE_OBJECT_ENVELOPE_FROM;
	if ( strcasecmp(identifier, "to") == 0 )
		return TESTSUITE_OBJECT_ENVELOPE_TO;
	if ( strcasecmp(identifier, "auth") == 0 )
		return TESTSUITE_OBJECT_ENVELOPE_AUTH_USER;	
	
	return -1;
}

static const char *tsto_envelope_get_member_name(int id) 
{
	switch ( id ) {
	case TESTSUITE_OBJECT_ENVELOPE_FROM: 
		return "from";
	case TESTSUITE_OBJECT_ENVELOPE_TO: 
		return "to";
	case TESTSUITE_OBJECT_ENVELOPE_AUTH_USER: 
		return "auth";
	}
	
	return NULL;
}

static bool tsto_envelope_set_member
(const struct sieve_runtime_env *renv ATTR_UNUSED, int id, string_t *value)
{
	switch ( id ) {
	case TESTSUITE_OBJECT_ENVELOPE_FROM: 
		testsuite_envelope_set_sender(str_c(value));
		return TRUE;
	case TESTSUITE_OBJECT_ENVELOPE_TO:
		testsuite_envelope_set_recipient(str_c(value));
		return TRUE;
	case TESTSUITE_OBJECT_ENVELOPE_AUTH_USER: 
		testsuite_envelope_set_auth_user(str_c(value));
		return TRUE;
	}
	
	return FALSE;
}
