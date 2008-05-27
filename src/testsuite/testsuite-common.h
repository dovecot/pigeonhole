#ifndef __TESTSUITE_COMMON_H
#define __TESTSUITE_COMMON_H

extern const struct sieve_extension testsuite_extension;

extern int ext_testsuite_my_id;

/* Testsuite message environment */

extern struct sieve_message_data testsuite_msgdata;

void testsuite_message_init(pool_t namespaces_pool, const char *user);
void testsuite_message_deinit(void);

void testsuite_message_set(string_t *message);

void testsuite_envelope_set_sender(const char *value);
void testsuite_envelope_set_recipient(const char *value);
void testsuite_envelope_set_auth_user(const char *value);

/* Testsuite validator context */

struct testsuite_validator_context {
	struct hash_table *object_registrations;
};

bool testsuite_validator_context_initialize(struct sieve_validator *valdtr);

/* Testsuite operands */

extern const struct sieve_operand testsuite_object_operand;

enum testsuite_operand_code {
	TESTSUITE_OPERAND_OBJECT
};

#endif /* __TESTSUITE_COMMON_H */
