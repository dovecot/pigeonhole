#ifndef __EXT_TESTSUITE_COMMON_H
#define __EXT_TESTSUITE_COMMON_H

extern const struct sieve_extension testsuite_extension;

extern int ext_testsuite_my_id;

/* Testsuite message environment */

extern struct sieve_message_data testsuite_msgdata;

void testsuite_message_init(pool_t namespaces_pool, const char *user);
void testsuite_message_deinit(void);

void testsuite_message_set(string_t *message);

#endif
