#ifndef __EXT_TESTSUITE_COMMON_H
#define __EXT_TESTSUITE_COMMON_H

extern const struct sieve_extension testsuite_extension;

extern int ext_testsuite_my_id;

/* Testsuite message environment */

extern struct sieve_message_data testsuite_msgdata;

void testsuite_message_init(pool_t namespaces_pool, const char *user);
void testsuite_message_deinit(void);

void testsuite_message_set(string_t *message);

/* Testsuite validator context */

bool testsuite_validator_context_initialize(struct sieve_validator *valdtr);

/* Testsuite operands */

struct testsuite_object_operand_interface {
	struct sieve_extension_obj_registry testsuite_objects;
};

extern const struct sieve_operand_class testsuite_object_oprclass;
extern const struct sieve_operand testsuite_object_operand;

enum testsuite_operand_code {
	TESTSUITE_OPERAND_OBJECT
};

/* Testsuite object access */

struct testsuite_object {
	const char *identifier;
	unsigned int code;
	const struct sieve_operand *operand;
	
	int (*get_member_id)(const char *identifier);
	bool (*set_member)(int id, string_t *value);
	string_t *(*get_member)(int id);
};

const struct testsuite_object *testsuite_object_find
	(struct sieve_validator *valdtr, const char *identifier, int *ext_id);
void testsuite_object_register
	(struct sieve_validator *valdtr, const struct testsuite_object *obj, 
		int ext_id);
		
const struct testsuite_object *testsuite_object_read
  (struct sieve_binary *sbin, sieve_size_t *address);
bool testsuite_object_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
	
bool testsuite_object_argument_activate
	(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
		struct sieve_command_context *cmd);

/* Testsuite core objects */

extern const struct testsuite_object message_testsuite_object;
extern const struct testsuite_object envelope_testsuite_object;

#endif
