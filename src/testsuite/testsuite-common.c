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

#include "testsuite-objects.h"
#include "testsuite-common.h"

/*
 * Global data
 */
 
/* Testsuite message environment */

struct sieve_message_data testsuite_msgdata;

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
	string_t *default_message = t_str_new(1024);
	str_append(default_message, _default_message_data);

	testsuite_user = user;
	mail_raw_init(namespaces_pool, user);
	_testsuite_message_set(default_message);
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
}

void testsuite_envelope_set_sender(const char *value)
{
	testsuite_msgdata.return_path = value;
}

void testsuite_envelope_set_recipient(const char *value)
{
	testsuite_msgdata.to_address = value;
}

void testsuite_envelope_set_auth_user(const char *value)
{
	testsuite_msgdata.auth_user = value;
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
	ctx->object_registrations = hash_create
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *) strcmp);

	testsuite_register_core_objects(pool, ctx);
	
	sieve_validator_extension_set_context(valdtr, ext_testsuite_my_id, ctx);

	return TRUE;
}

/* 
 * Generator context 
 */

bool testsuite_generator_context_initialize(struct sieve_generator *gentr)
{
	pool_t pool = sieve_validator_pool(gentr);
	struct sieve_binary *sbin = sieve_generator_get_binary(gentr);
	struct testsuite_generator_context *ctx = 
		p_new(pool, struct testsuite_generator_context, 1);
	
	/* Setup exit jumplist */
	ctx->exit_jumps = sieve_jumplist_create(pool, sbin);
	
	sieve_generator_extension_set_context(gentr, ext_testsuite_my_id, ctx);

	return TRUE;
}
