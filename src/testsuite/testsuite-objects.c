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
#include "sieve-extensions-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-dump.h"

#include "testsuite-common.h"
#include "testsuite-objects.h"

/* 
 * Types
 */
 
struct testsuite_object_registration {
	int ext_id;
	const struct testsuite_object *object;
};
 
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
 * Forward declarations
 */
 
static void _testsuite_object_register
	(pool_t pool, struct testsuite_validator_context *ctx, 
		const struct testsuite_object *obj, int ext_id);

/* 
 * Testsuite object registry
 */
 
static inline struct testsuite_validator_context *
	_get_validator_context(struct sieve_validator *valdtr)
{
	return (struct testsuite_validator_context *) 
		sieve_validator_extension_get_context(valdtr, ext_testsuite_my_id);
}
 
static void _testsuite_object_register
(pool_t pool, struct testsuite_validator_context *ctx, 
	const struct testsuite_object *obj, int ext_id) 
{	
	struct testsuite_object_registration *reg;
	
	reg = p_new(pool, struct testsuite_object_registration, 1);
	reg->object = obj;
	reg->ext_id = ext_id;

	hash_insert(ctx->object_registrations, (void *) obj->identifier, (void *) reg);
}
 
void testsuite_object_register
(struct sieve_validator *valdtr, const struct testsuite_object *obj, int ext_id) 
{
	pool_t pool = sieve_validator_pool(valdtr);
	struct testsuite_validator_context *ctx = _get_validator_context(valdtr);

	_testsuite_object_register(pool, ctx, obj, ext_id);
}

const struct testsuite_object *testsuite_object_find
(struct sieve_validator *valdtr, const char *identifier, int *ext_id) 
{
	struct testsuite_validator_context *ctx = _get_validator_context(valdtr);		
	struct testsuite_object_registration *reg =
		(struct testsuite_object_registration *) 
			hash_lookup(ctx->object_registrations, identifier);
			
	if ( reg == NULL ) return NULL;

	if ( ext_id != NULL ) *ext_id = reg->ext_id;

  return reg->object;
}

void testsuite_register_core_objects
	(pool_t pool, struct testsuite_validator_context *ctx)
{
	unsigned int i;
	
	/* Register core testsuite objects */
	for ( i = 0; i < testsuite_core_objects_count; i++ ) {
		const struct testsuite_object *object = testsuite_core_objects[i];
		
		_testsuite_object_register(pool, ctx, object, ext_testsuite_my_id);
	}
}
 
/* 
 * Testsuite object code
 */ 
 
const struct sieve_operand_class testsuite_object_oprclass = 
	{ "testsuite-object" };

const struct testsuite_object_operand_interface testsuite_object_oprint = {
	SIEVE_EXT_DEFINE_OBJECTS(testsuite_core_objects)
};

const struct sieve_operand testsuite_object_operand = { 
	"testsuite-object",
	&testsuite_extension, 
	TESTSUITE_OPERAND_OBJECT, 
	&testsuite_object_oprclass,
	&testsuite_object_oprint
};

static void testsuite_object_emit
(struct sieve_binary *sbin, const struct testsuite_object *obj, int ext_id,
	int member_id)
{ 
	(void) sieve_operand_emit_code(sbin, obj->operand, ext_id);	
	(void) sieve_binary_emit_byte(sbin, obj->code);
	
	if ( obj->get_member_id != NULL ) {
		(void) sieve_binary_emit_byte(sbin, (unsigned char) member_id);
	}
}

const struct testsuite_object *testsuite_object_read
(struct sieve_binary *sbin, sieve_size_t *address)
{
	const struct sieve_operand *operand;
	const struct testsuite_object_operand_interface *intf;	
	unsigned int obj_code; 

	operand = sieve_operand_read(sbin, address);
	if ( operand == NULL || operand->class != &testsuite_object_oprclass ) 
		return NULL;
	
	intf = operand->interface;
	if ( intf == NULL ) 
		return NULL;
			
	if ( !sieve_binary_read_byte(sbin, address, &obj_code) ) 
		return NULL;

	return sieve_extension_get_object
		(struct testsuite_object, intf->testsuite_objects, obj_code);
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
		return object->identifier;
		
	if ( member == NULL )	
		return t_strdup_printf("%s.%d", object->identifier, member_id);
	
	return t_strdup_printf("%s.%s", object->identifier, member);
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
	
	sieve_code_dumpf(denv, "TESTSUITE_OBJECT: %s", 
		testsuite_object_member_name(object, member_id));
	
	return TRUE;
}

/* 
 * Testsuite object argument
 */
 
static bool arg_testsuite_object_generate
	(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
		struct sieve_command_context *cmd);

const struct sieve_argument testsuite_object_argument = 
	{ "testsuite-object", NULL, NULL, NULL, arg_testsuite_object_generate };
 
struct testsuite_object_argctx {
	const struct testsuite_object *object;
	int ext_id;
	int member;
};

bool testsuite_object_argument_activate
(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
	struct sieve_command_context *cmd) 
{
	const char *objname = sieve_ast_argument_strc(arg);
	const struct testsuite_object *object;
	int ext_id, member_id;
	const char *member;
	struct testsuite_object_argctx *ctx;
	
	/* Parse the object specifier */
	
	member = strchr(objname, '.');
	if ( member != NULL ) {
		objname = t_strdup_until(objname, member);
		member++;
	}
	
	/* Find the object */
	
	object = testsuite_object_find(valdtr, objname, &ext_id);
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
	ctx->ext_id = ext_id;
	ctx->member = member_id;
	
	arg->argument = &testsuite_object_argument;
	arg->context = (void *) ctx;
	
	return TRUE;
}

static bool arg_testsuite_object_generate
	(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	struct testsuite_object_argctx *ctx = 
		(struct testsuite_object_argctx *) arg->context;
	
	testsuite_object_emit(sbin, ctx->object, ctx->ext_id, ctx->member);
		
	return TRUE;
}

/* 
 * Testsuite core object implementation
 */
 
static bool tsto_message_set_member(int id, string_t *value);

static int tsto_envelope_get_member_id(const char *identifier);
static const char *tsto_envelope_get_member_name(int id);
static bool tsto_envelope_set_member(int id, string_t *value);


const struct testsuite_object message_testsuite_object = { 
	"message",
	TESTSUITE_OBJECT_MESSAGE,
	&testsuite_object_operand,
	NULL, NULL, 
	tsto_message_set_member, 
	NULL
};

const struct testsuite_object envelope_testsuite_object = { 
	"envelope",
	TESTSUITE_OBJECT_ENVELOPE,
	&testsuite_object_operand,
	tsto_envelope_get_member_id, 
	tsto_envelope_get_member_name,
	tsto_envelope_set_member, 
	NULL
};

static bool tsto_message_set_member(int id, string_t *value) 
{
	if ( id != -1 ) return FALSE;
	
	testsuite_message_set(value);
	
	return TRUE;
}

static int tsto_envelope_get_member_id(const char *identifier)
{
	if ( strcasecmp(identifier, "from") == 0 )
		return 0;
	if ( strcasecmp(identifier, "to") == 0 )
		return 1;
	if ( strcasecmp(identifier, "auth") == 0 )
		return 2;	
	
	return -1;
}

static const char *tsto_envelope_get_member_name(int id) 
{
	switch ( id ) {
	case 0: return "from";
	case 1: return "to";
	case 2: return "auth";
	}
	
	return NULL;
}

static bool tsto_envelope_set_member(int id, string_t *value)
{
	switch ( id ) {
	case 0: 
		testsuite_envelope_set_sender(str_c(value));
		return TRUE;
	case 1:
		testsuite_envelope_set_recipient(str_c(value));
		return TRUE;
	case 2: 
		testsuite_envelope_set_auth_user(str_c(value));
		return TRUE;
	}
	
	return FALSE;
}
