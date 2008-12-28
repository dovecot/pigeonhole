/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "string.h"
#include "ostream.h"
#include "hash.h"
#include "mail-storage.h"
#include "env-util.h"

#include "mail-raw.h"

#include "sieve.h"
#include "sieve-error-private.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-commands.h"
#include "sieve-extensions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"
#include "sieve-dump.h"

#include "testsuite-common.h"
#include "testsuite-objects.h"
#include "testsuite-result.h"

/*
 * Global data
 */
 
/* Testsuite message environment */

struct sieve_message_data testsuite_msgdata;

/* Test context */

static string_t *test_name;
unsigned int test_index;
unsigned int test_failures;

/* Tested script context */

static struct sieve_error_handler *test_script_ehandler;

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
	 
	_raw_message = mail_raw_open_data(message);
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

	memset(&testsuite_msgdata, 0, sizeof(testsuite_msgdata));	
	testsuite_msgdata.mail = mail;
	testsuite_msgdata.auth_user = testsuite_user;
	testsuite_msgdata.return_path = sender;
	testsuite_msgdata.to_address = recipient;

	(void)mail_get_first_header(mail, "Message-ID", &testsuite_msgdata.id);
}

void testsuite_message_init(const char *user)
{		
	message_pool = pool_alloconly_create("testsuite_message", 6096);

	string_t *default_message = str_new(message_pool, 1024);
	str_append(default_message, _default_message_data);

	testsuite_user = user;
	mail_raw_init(user);
	_testsuite_message_set(default_message);

	envelope_to = str_new(message_pool, 256);
	envelope_from = str_new(message_pool, 256);
	envelope_auth = str_new(message_pool, 256);
}

void testsuite_message_set
(const struct sieve_runtime_env *renv, string_t *message)
{
	mail_raw_close(_raw_message);

	_testsuite_message_set(message);	

	sieve_message_context_flush(renv->msgctx);
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
 
static void testsuite_test_context_init(void)
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
			printf("%2d: Test FAILED\n", test_index);
		else
			printf("%2d: Test FAILED: %s\n", test_index, str_c(reason));
	} else {
		if ( reason == NULL || str_len(reason) == 0 )
			printf("%2d: Test '%s' FAILED\n", test_index, str_c(test_name));
		else
			printf("%2d: Test '%s' FAILED: %s\n", test_index, 
				str_c(test_name), str_c(reason));
	}

	str_truncate(test_name, 0);

	test_failures++;
}

void testsuite_testcase_fail(const char *reason)
{	
	if ( reason == NULL || *reason == '\0' )
		printf("XX: Test CASE FAILED\n");
	else
		printf("XX: Test CASE FAILED: %s\n", reason);

	test_failures++;
}

void testsuite_test_succeed(string_t *reason)
{
	if ( str_len(test_name) == 0 ) {
		if ( reason == NULL || str_len(reason) == 0 )
			printf("%2d: Test SUCCEEDED\n", test_index);
		else
			printf("%2d: Test SUCCEEDED: %s\n", test_index, str_c(reason));
	} else {
		if ( reason == NULL || str_len(reason) == 0 )
			printf("%2d: Test '%s' SUCCEEDED\n", test_index, str_c(test_name));
		else
			printf("%2d: Test '%s' SUCCEEDED: %s\n", test_index, 
				str_c(test_name), str_c(reason));
	}
	str_truncate(test_name, 0);
}

static void testsuite_test_context_deinit(void)
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

/*
 * Tested script environment
 */ 

/* Special error handler */

struct _testsuite_script_message {
	const char *location;
	const char *message;
};

unsigned int _testsuite_script_error_index = 0;

static pool_t _testsuite_scriptmsg_pool = NULL;
ARRAY_DEFINE(_testsuite_script_errors, struct _testsuite_script_message);

struct sieve_binary *_testsuite_compiled_script;

static void _testsuite_script_verror
(struct sieve_error_handler *ehandler ATTR_UNUSED, const char *location,
    const char *fmt, va_list args)
{
	pool_t pool = _testsuite_scriptmsg_pool;
	struct _testsuite_script_message msg;

	msg.location = p_strdup(pool, location);
	msg.message = p_strdup_vprintf(pool, fmt, args);

//	printf("error: %s: %s.\n", location, t_strdup_vprintf(fmt, args));
	
	array_append(&_testsuite_script_errors, &msg, 1);	
}

static struct sieve_error_handler *_testsuite_script_ehandler_create(void)
{
	pool_t pool;
	struct sieve_error_handler *ehandler;

	/* Pool is not strictly necessary, but other handler types will need a pool,
	 * so this one will have one too.
	 */
	pool = pool_alloconly_create
		("testsuite_script_error_handler", sizeof(struct sieve_error_handler));
	ehandler = p_new(pool, struct sieve_error_handler, 1);
	sieve_error_handler_init(ehandler, pool, 0);

	ehandler->verror = _testsuite_script_verror;

	return ehandler;
}

static void testsuite_script_clear_messages(void)
{
	if ( _testsuite_scriptmsg_pool != NULL ) {
		if ( array_count(&_testsuite_script_errors) == 0 )
			return;
		pool_unref(&_testsuite_scriptmsg_pool);
	}

	_testsuite_scriptmsg_pool = pool_alloconly_create
		("testsuite_script_messages", 8192);
	
	p_array_init(&_testsuite_script_errors, _testsuite_scriptmsg_pool, 128);	

	sieve_error_handler_reset(test_script_ehandler);
}

void testsuite_script_get_error_init(void)
{
	_testsuite_script_error_index = 0;
}

const char *testsuite_script_get_error_next(bool location)
{
	const struct _testsuite_script_message *msg;

	if ( _testsuite_script_error_index >= array_count(&_testsuite_script_errors) )
		return NULL;

	msg = array_idx(&_testsuite_script_errors, _testsuite_script_error_index++);

	if ( location ) 
		return msg->location;

	return msg->message;		
}

static void testsuite_script_init(void)
{
	test_script_ehandler = _testsuite_script_ehandler_create(); 	
	sieve_error_handler_accept_infolog(test_script_ehandler, TRUE);

	testsuite_script_clear_messages();

	_testsuite_compiled_script = NULL;
}

bool testsuite_script_compile(const char *script_path)
{
	struct sieve_binary *sbin;
	const char *sieve_dir;

	testsuite_script_clear_messages();

	/* Initialize environment */
	sieve_dir = strrchr(script_path, '/');
	if ( sieve_dir == NULL )
		sieve_dir= "./";
	else
		sieve_dir = t_strdup_until(script_path, sieve_dir+1);

	/* Currently needed for include (FIXME) */
	env_put(t_strconcat("SIEVE_DIR=", sieve_dir, "included", NULL));
	env_put(t_strconcat("SIEVE_GLOBAL_DIR=", sieve_dir, "included-global", NULL));

	if ( (sbin = sieve_compile(script_path, test_script_ehandler)) == NULL )
		return FALSE;

	if ( _testsuite_compiled_script != NULL ) {
		sieve_close(&_testsuite_compiled_script);
	}

	_testsuite_compiled_script = sbin;

	return TRUE;
}

bool testsuite_script_run(const struct sieve_runtime_env *renv)
{
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	struct sieve_result *result;
	struct sieve_interpreter *interp;
	int ret;

	if ( _testsuite_compiled_script == NULL ) {
		sieve_runtime_error(renv, sieve_error_script_location(renv->script,0),
			"testsuite: no script compiled yet");
		return FALSE;
	}

	testsuite_script_clear_messages();

	/* Compose script execution environment */
	memset(&scriptenv, 0, sizeof(scriptenv));
	scriptenv.default_mailbox = "INBOX";
	scriptenv.namespaces = NULL;
	scriptenv.username = "user";
	scriptenv.hostname = "host.example.com";
	scriptenv.postmaster_address = "postmaster@example.com";
	scriptenv.smtp_open = NULL;
	scriptenv.smtp_close = NULL;
	scriptenv.duplicate_mark = NULL;
	scriptenv.duplicate_check = NULL;
	
	result = sieve_result_create(test_script_ehandler);

	/* Execute the script */
	interp=sieve_interpreter_create(_testsuite_compiled_script, test_script_ehandler, NULL);
	
	if ( interp == NULL )
		return SIEVE_EXEC_BIN_CORRUPT;
		
	ret = sieve_interpreter_run(interp, renv->msgdata, &scriptenv, &result, &estatus);

	sieve_interpreter_free(&interp);

	testsuite_result_assign(result);
	
	return ( ret > 0 );
}

static void testsuite_script_deinit(void)
{
	sieve_error_handler_unref(&test_script_ehandler);

	if ( _testsuite_compiled_script != NULL ) {
		sieve_close(&_testsuite_compiled_script);
	}

	pool_unref(&_testsuite_scriptmsg_pool);
	//str_free(test_script_error_buf);
}

/*
 * Main testsuite init/deinit
 */

void testsuite_init(void)
{
	testsuite_test_context_init();
	testsuite_script_init();
	testsuite_result_init();
}

void testsuite_deinit(void)
{
	testsuite_result_deinit();
	testsuite_script_deinit();
	testsuite_test_context_deinit();
}

