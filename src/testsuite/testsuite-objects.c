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
	(struct sieve_binary *sbin, const struct testsuite_object *obj, int ext_id)
{ 
	(void) sieve_operand_emit_code(sbin, obj->operand, ext_id);	
	(void) sieve_binary_emit_byte(sbin, obj->code);
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

bool testsuite_object_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct testsuite_object *object;

	sieve_code_mark(denv);
		
	if ( (object = testsuite_object_read(denv->sbin, address)) == NULL )
		return FALSE;
	
	sieve_code_dumpf(denv, "TESTSUITE_OBJECT: %s", object->identifier);
		
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
};

bool testsuite_object_argument_activate
(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
	struct sieve_command_context *cmd) 
{
	const char *objname = sieve_ast_argument_strc(arg);
	const struct testsuite_object *object;
	int ext_id;
	struct testsuite_object_argctx *ctx;
	
	object = testsuite_object_find(valdtr, objname, &ext_id);
	if ( object == NULL ) {
		sieve_command_validate_error(valdtr, cmd, 
			"unknown testsuite object '%s'", objname);
		return FALSE;
	}
	
	ctx = p_new(sieve_command_pool(cmd), struct testsuite_object_argctx, 1);
	ctx->object = object;
	ctx->ext_id = ext_id;
	
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
	
	testsuite_object_emit(sbin, ctx->object, ctx->ext_id);
		
	return TRUE;
}

/* 
 * Testsuite core object implementation
 */
 
static bool tsto_message_set_member(int id, string_t *value);

const struct testsuite_object message_testsuite_object = { 
	"message",
	TESTSUITE_OBJECT_MESSAGE,
	&testsuite_object_operand,
	NULL, tsto_message_set_member, NULL
};

const struct testsuite_object envelope_testsuite_object = { 
	"envelope",
	TESTSUITE_OBJECT_ENVELOPE,
	&testsuite_object_operand,
	NULL, NULL, NULL
};

static bool tsto_message_set_member(int id, string_t *value) 
{
	if ( id != -1 ) return FALSE;
	
	testsuite_message_set(value);
	
	return TRUE;
}
