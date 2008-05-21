#include "lib.h"
#include "string.h"
#include "ostream.h"
#include "mail-storage.h"

#include "mail-raw.h"
#include "namespaces.h"
#include "sieve.h"

#include "testsuite-common.h"

/* Message environment */

struct sieve_message_data testsuite_msgdata;

static const char *testsuite_user;
static struct mail_raw *_raw_message;

/* Testsuite message environment */

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

	memset(&testsuite_msgdata, 0, sizeof(testsuite_msgdata));
	testsuite_msgdata.mail = mail;
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
