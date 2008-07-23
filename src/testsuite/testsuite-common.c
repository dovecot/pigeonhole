#include "lib.h"
#include "str.h"
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
#include "sieve-dump.h"

#include "testsuite-objects.h"
#include "testsuite-common.h"

/*
 * Global data
 */
 
/* Testsuite message environment */

struct sieve_message_data testsuite_msgdata;

/* Test context */

static string_t *test_name;
unsigned int test_index;
unsigned int test_failures;

/* 
 * Testsuite message environment 
 */

static const char *testsuite_user;
static struct mail_raw *_raw_message;

static const char *_default_message_data = 
"From: stephan@rename-it.nl\n"
"To: sirius@drunksnipers.com\n"
"Subject: Frop!\n"
"\n"
"Friep!\n";

static string_t *envelope_from;
static string_t *envelope_to;
static string_t *envelope_auth;

pool_t message_pool;

static void _testsuite_message_set(string_t *message)
{
	struct mail *mail;
	const char *recipient = NULL, *sender = NULL;
	
	/*
	 * Open message as mail struct
	 */
	 
	_raw_message = mail_raw_open(message);
	mail = _raw_message->mail;

	/* 
	 * Collect necessary message data 
	 */
	 
	/* Get recipient address */ 
	(void)mail_get_first_header(mail, "Envelope-To", &recipient);
	if ( recipient == NULL )
		(void)mail_get_first_header(mail, "To", &recipient);
	if ( recipient == NULL ) 
		recipient = "recipient@example.com";
	
	/* Get sender address */
	(void)mail_get_first_header(mail, "Return-path", &sender);
	if ( sender == NULL ) 
		(void)mail_get_first_header(mail, "Sender", &sender);
	if ( sender == NULL ) 
		(void)mail_get_first_header(mail, "From", &sender);
	if ( sender == NULL ) 
		sender = "sender@example.com";

	memset(&testsuite_msgdata, 0, sizeof(testsuite_msgdata));	testsuite_msgdata.mail = mail;
	testsuite_msgdata.auth_user = testsuite_user;
	testsuite_msgdata.return_path = sender;
	testsuite_msgdata.to_address = recipient;

	(void)mail_get_first_header(mail, "Message-ID", &testsuite_msgdata.id);
}

void testsuite_message_init(pool_t namespaces_pool, const char *user)
{		
	message_pool = pool_alloconly_create("testsuite_message", 6096);

	string_t *default_message = str_new(message_pool, 1024);
	str_append(default_message, _default_message_data);

	testsuite_user = user;
	mail_raw_init(namespaces_pool, user);
	_testsuite_message_set(default_message);

	envelope_to = str_new(message_pool, 256);
	envelope_from = str_new(message_pool, 256);
	envelope_auth = str_new(message_pool, 256);
}

void testsuite_message_set(string_t *message)
{
	mail_raw_close(_raw_message);

	_testsuite_message_set(message);	
}

void testsuite_message_deinit(void)
{
	mail_raw_close(_raw_message);
	mail_raw_deinit();

	pool_unref(&message_pool);
}

void testsuite_envelope_set_sender(const char *value)
{
	str_truncate(envelope_from, 0);
	str_append(envelope_from, value);
	testsuite_msgdata.return_path = str_c(envelope_from);
}

void testsuite_envelope_set_recipient(const char *value)
{
	str_truncate(envelope_to, 0);
	str_append(envelope_to, value);
	testsuite_msgdata.to_address = str_c(envelope_to);
}

void testsuite_envelope_set_auth_user(const char *value)
{
	str_truncate(envelope_auth, 0);
	str_append(envelope_auth, value);
	testsuite_msgdata.auth_user = str_c(envelope_auth);
}

/* 
 * Validator context 
 */

bool testsuite_validator_context_initialize(struct sieve_validator *valdtr)
{
	pool_t pool = sieve_validator_pool(valdtr);
	struct testsuite_validator_context *ctx = 
		p_new(pool, struct testsuite_validator_context, 1);
	
	/* Setup object registry */
	ctx->object_registrations = sieve_validator_object_registry_create(valdtr);
	testsuite_register_core_objects(ctx);
	
	sieve_validator_extension_set_context(valdtr, &testsuite_extension, ctx);

	return TRUE;
}

struct testsuite_validator_context *testsuite_validator_context_get
(struct sieve_validator *valdtr)
{
	return (struct testsuite_validator_context *)
		sieve_validator_extension_get_context(valdtr, &testsuite_extension);
}

/* 
 * Generator context 
 */

bool testsuite_generator_context_initialize(struct sieve_generator *gentr)
{
	pool_t pool = sieve_generator_pool(gentr);
	struct sieve_binary *sbin = sieve_generator_get_binary(gentr);
	struct testsuite_generator_context *ctx = 
		p_new(pool, struct testsuite_generator_context, 1);
	
	/* Setup exit jumplist */
	ctx->exit_jumps = sieve_jumplist_create(pool, sbin);
	
	sieve_generator_extension_set_context(gentr, &testsuite_extension, ctx);

	return TRUE;
}

/*
 * Test context
 */
 
void testsuite_test_context_init(void)
{
	test_name = str_new(default_pool, 128);
	test_index = 0;	
	test_failures = 0;
}

void testsuite_test_start(string_t *name)
{
	str_truncate(test_name, 0);
	str_append_str(test_name, name);

	test_index++;
}

void testsuite_test_fail(string_t *reason)
{	
	if ( str_len(test_name) == 0 ) {
		if ( reason == NULL || str_len(reason) == 0 )
			printf("%d: Test FAILED\n", test_index);
		else
			printf("%d: Test FAILED: %s\n", test_index, str_c(reason));
	} else {
		if ( reason == NULL || str_len(reason) == 0 )
			printf("%d: Test '%s' FAILED\n", test_index, str_c(test_name));
		else
			printf("%d: Test '%s' FAILED: %s\n", test_index, 
				str_c(test_name), str_c(reason));
	}

	str_truncate(test_name, 0);

	test_failures++;
}

void testsuite_test_succeed(string_t *reason)
{
	if ( str_len(test_name) == 0 ) {
		if ( reason == NULL || str_len(reason) == 0 )
			printf("%d: Test SUCCEEDED\n", test_index);
		else
			printf("%d: Test SUCCEEDED: %s\n", test_index, str_c(reason));
	} else {
		if ( reason == NULL || str_len(reason) == 0 )
			printf("%d: Test '%s' SUCCEEDED\n", test_index, str_c(test_name));
		else
			printf("%d: Test '%s' SUCCEEDED: %s\n", test_index, 
				str_c(test_name), str_c(reason));
	}
	str_truncate(test_name, 0);
}

void testsuite_test_context_deinit(void)
{
	//str_free(test_name);
}

int testsuite_testcase_result(void)
{
	if ( test_failures > 0 ) {
		printf("\nFAIL: %d of %d tests failed.\n\n", test_failures, test_index);
		return 1;
	}

	printf("\nPASS: %d tests succeeded.\n\n", test_index);
	return 0;
}

