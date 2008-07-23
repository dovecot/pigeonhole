#ifndef __TESTSUITE_OBJECTS_H
#define __TESTSUITE_OBJECTS_H

#include "sieve-common.h"
#include "sieve-objects.h"

#include "testsuite-common.h"

/* Testsuite object operand */

struct testsuite_object_operand_interface {
	struct sieve_extension_obj_registry testsuite_objects;
};

extern const struct sieve_operand_class testsuite_object_oprclass;

/* Testsuite object access */

struct testsuite_object {
	struct sieve_object object;
	
	int (*get_member_id)(const char *identifier);
	const char *(*get_member_name)(int id);
	bool (*set_member)(int id, string_t *value);
	string_t *(*get_member)(int id);
};

/* Testsuite object registration */

const struct testsuite_object *testsuite_object_find
	(struct sieve_validator *valdtr, const char *identifier);
void testsuite_object_register
	(struct sieve_validator *valdtr, const struct testsuite_object *tobj);		
void testsuite_register_core_objects
	(struct testsuite_validator_context *ctx);
		
/* Testsuite object argument */		
	
bool testsuite_object_argument_activate
	(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
		struct sieve_command_context *cmd);		
		
/* Testsuite object code */

const struct testsuite_object *testsuite_object_read
  (struct sieve_binary *sbin, sieve_size_t *address);
const struct testsuite_object *testsuite_object_read_member
  (struct sieve_binary *sbin, sieve_size_t *address, int *member_id);
const char *testsuite_object_member_name
	(const struct testsuite_object *object, int member_id);
bool testsuite_object_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

/* Testsuite core objects */

extern const struct testsuite_object message_testsuite_object;
extern const struct testsuite_object envelope_testsuite_object;

#endif /* __TESTSUITE_OBJECTS_H */
