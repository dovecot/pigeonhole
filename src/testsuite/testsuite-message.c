/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "mail-storage.h"

#include "mail-raw.h"

#include "sieve-common.h"
#include "sieve-message.h"
#include "sieve-interpreter.h"

#include "testsuite-common.h"
#include "testsuite-message.h"

/* 
 * Testsuite message environment 
 */
 
struct sieve_message_data testsuite_msgdata;

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
