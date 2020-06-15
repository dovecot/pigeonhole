/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "istream.h"
#include "smtp-params.h"
#include "message-address.h"
#include "mail-storage.h"
#include "master-service.h"
#include "mail-raw.h"

#include "sieve-common.h"
#include "sieve-address.h"
#include "sieve-error.h"
#include "sieve-message.h"
#include "sieve-interpreter.h"

#include "sieve-tool.h"

#include "testsuite-common.h"
#include "testsuite-message.h"

/*
 * Testsuite message environment
 */

struct testsuite_message {
	struct testsuite_message *next;

	struct mail_raw *mail_raw;
};

struct sieve_message_data testsuite_msgdata;
static struct smtp_params_rcpt testsuite_rcpt_params;

static struct testsuite_message *testsuite_msg;

static const char *_default_message_data =
"From: sender@example.com\n"
"To: recipient@example.org\n"
"Subject: Frop!\n"
"\n"
"Friep!\n";

static struct smtp_address *testsuite_env_mail_from = NULL;
static struct smtp_address *testsuite_env_rcpt_to = NULL;
static struct smtp_address *testsuite_env_orig_rcpt_to = NULL;
static char *testsuite_env_auth = NULL;

static pool_t testsuite_msg_pool;
static string_t *testsuite_msg_default;
static char *testsuite_msg_id = NULL;

static const struct smtp_address *
testsuite_message_get_address(struct mail *mail, const char *header)
{
	struct message_address *addr;
	struct smtp_address *smtp_addr;
	const char *str;

	if (mail_get_first_header(mail, header, &str) <= 0)
		return NULL;
	addr = message_address_parse(pool_datastack_create(),
				     (const unsigned char *)str,
				     strlen(str), 1, 0);
	if (addr == NULL || addr->mailbox == NULL || *addr->mailbox == '\0')
		return NULL;
	if (smtp_address_create_from_msg_temp(addr, &smtp_addr) < 0)
		return NULL;
	return smtp_addr;
}

static void testsuite_message_set_data(struct mail *mail)
{
	const struct smtp_address *recipient = NULL, *sender = NULL;
	const char *msg_id;

	static const struct smtp_address default_recipient = {
		.localpart = "recipient",
		.domain = "example.com",
	};
	static const struct smtp_address default_sender = {
		.localpart = "sender",
		.domain = "example.com",
	};

	i_free(testsuite_env_mail_from);
	i_free(testsuite_env_rcpt_to);
	i_free(testsuite_env_orig_rcpt_to);
	i_free(testsuite_env_auth);
	i_free(testsuite_msg_id);

	/*
	 * Collect necessary message data
	 */

	/* Get recipient address */
	recipient = testsuite_message_get_address(mail, "Envelope-To");
	if (recipient == NULL)
		recipient = testsuite_message_get_address(mail, "To");
	if (recipient == NULL)
		recipient = &default_recipient;

	/* Get sender address */
	sender = testsuite_message_get_address(mail, "Return-path");
	if (sender == NULL)
		sender = testsuite_message_get_address(mail, "Sender");
	if (sender == NULL)
		sender = testsuite_message_get_address(mail, "From");
	if (sender == NULL)
		sender = &default_sender;

	testsuite_env_mail_from = smtp_address_clone(default_pool, sender);
	testsuite_env_rcpt_to = smtp_address_clone(default_pool, recipient);
	testsuite_env_orig_rcpt_to = smtp_address_clone(default_pool, recipient);

	(void)mail_get_message_id(mail, &msg_id);
	testsuite_msg_id = i_strdup(msg_id);

	i_zero(&testsuite_msgdata);
	testsuite_msgdata.mail = mail;
	testsuite_msgdata.auth_user = sieve_tool_get_username(sieve_tool);
	testsuite_msgdata.envelope.mail_from = testsuite_env_mail_from;
	testsuite_msgdata.envelope.rcpt_to = testsuite_env_rcpt_to;
	testsuite_msgdata.id = testsuite_msg_id;

	i_zero(&testsuite_rcpt_params);
	testsuite_rcpt_params.orcpt.addr = testsuite_env_orig_rcpt_to;

	testsuite_msgdata.envelope.rcpt_params = &testsuite_rcpt_params;
}

static struct testsuite_message *testsuite_message_new(void)
{
	struct testsuite_message *msg;

	msg = i_new(struct testsuite_message, 1);
	msg->next = testsuite_msg;
	testsuite_msg = msg;

	return msg;
}

static void testsuite_message_new_string(string_t *mail_str)
{
	struct mail_user *mail_raw_user =
		sieve_tool_get_mail_raw_user(sieve_tool);
	struct testsuite_message *msg;

	msg = testsuite_message_new();
	msg->mail_raw = mail_raw_open_data(mail_raw_user, mail_str);

	testsuite_message_set_data(msg->mail_raw->mail);
}

static void testsuite_message_new_file(const char *mail_path)
{
	struct mail_user *mail_raw_user =
		sieve_tool_get_mail_raw_user(sieve_tool);
	struct testsuite_message *msg;

	msg = testsuite_message_new();
	msg->mail_raw = mail_raw_open_file(mail_raw_user, mail_path);

	testsuite_message_set_data(msg->mail_raw->mail);
}

static void testsuite_message_free(bool all)
{
	struct testsuite_message *msg;

	if (testsuite_msg == NULL)
		return;

	msg = (all ? testsuite_msg : testsuite_msg->next);
	while (msg != NULL) {
		struct testsuite_message *msg_next = msg->next;

		mail_raw_close(&msg->mail_raw);
		i_free(msg);

		msg = msg_next;
	}
	if (all)
		testsuite_msg = NULL;
	else
		testsuite_msg->next = NULL;
}

void testsuite_message_flush(void)
{
	testsuite_message_free(FALSE);
}

void testsuite_message_init(void)
{
	testsuite_msg_pool = pool_alloconly_create("testsuite_message", 6096);

	testsuite_msg_default = str_new(testsuite_msg_pool, 1024);
	str_append(testsuite_msg_default, _default_message_data);

	testsuite_message_new_string(testsuite_msg_default);
}

void testsuite_message_set_default(const struct sieve_runtime_env *renv)
{
	sieve_message_context_reset(renv->msgctx);

	testsuite_message_new_string(testsuite_msg_default);
}

void testsuite_message_set_string(const struct sieve_runtime_env *renv,
				  string_t *message)
{
	sieve_message_context_reset(renv->msgctx);

	testsuite_message_new_string(message);
}

void testsuite_message_set_file(const struct sieve_runtime_env *renv,
				const char *file_path)
{
	sieve_message_context_reset(renv->msgctx);

	testsuite_message_new_file(file_path);
}

void testsuite_message_set_mail(const struct sieve_runtime_env *renv,
				struct mail *mail)
{
	sieve_message_context_reset(renv->msgctx);

	testsuite_message_set_data(mail);
}

void testsuite_message_deinit(void)
{
	testsuite_message_free(TRUE);

	i_free(testsuite_env_mail_from);
	i_free(testsuite_env_rcpt_to);
	i_free(testsuite_env_orig_rcpt_to);
	i_free(testsuite_env_auth);
	pool_unref(&testsuite_msg_pool);
	i_free(testsuite_msg_id);
}

void testsuite_envelope_set_sender_address(const struct sieve_runtime_env *renv,
					   const struct smtp_address *address)
{
	sieve_message_context_reset(renv->msgctx);

	i_free(testsuite_env_mail_from);

	testsuite_env_mail_from = smtp_address_clone(default_pool, address);
	testsuite_msgdata.envelope.mail_from = testsuite_env_mail_from;
}

void testsuite_envelope_set_sender(const struct sieve_runtime_env *renv,
				   const char *value)
{
	struct smtp_address *address = NULL;
	const char *error;

	if (smtp_address_parse_path(pool_datastack_create(), value,
				    (SMTP_ADDRESS_PARSE_FLAG_ALLOW_EMPTY |
				     SMTP_ADDRESS_PARSE_FLAG_BRACKETS_OPTIONAL),
				    &address, &error) < 0) {
		e_error(testsuite_sieve_instance->event,
			"testsuite: envelope sender address "
			"'%s' is invalid: %s", value, error);
	}
	testsuite_envelope_set_sender_address(renv, address);
}

void testsuite_envelope_set_recipient_address(
	const struct sieve_runtime_env *renv,
	const struct smtp_address *address)
{
	sieve_message_context_reset(renv->msgctx);

	i_free(testsuite_env_rcpt_to);
	i_free(testsuite_env_orig_rcpt_to);

	testsuite_env_rcpt_to = smtp_address_clone(default_pool, address);
	testsuite_env_orig_rcpt_to = smtp_address_clone(default_pool, address);
	testsuite_msgdata.envelope.rcpt_to = testsuite_env_rcpt_to;
	testsuite_rcpt_params.orcpt.addr = testsuite_env_orig_rcpt_to;
}

void testsuite_envelope_set_recipient(const struct sieve_runtime_env *renv,
				      const char *value)
{
	struct smtp_address *address = NULL;
	const char *error;

	if (smtp_address_parse_path(pool_datastack_create(), value,
				    (SMTP_ADDRESS_PARSE_FLAG_ALLOW_LOCALPART |
				     SMTP_ADDRESS_PARSE_FLAG_BRACKETS_OPTIONAL),
				    &address, &error) < 0) {
		e_error(testsuite_sieve_instance->event,
			"testsuite: envelope recipient address "
			"'%s' is invalid: %s", value, error);
	}
	testsuite_envelope_set_recipient_address(renv, address);
}

void testsuite_envelope_set_orig_recipient_address(
	const struct sieve_runtime_env *renv,
	const struct smtp_address *address)
{
	sieve_message_context_reset(renv->msgctx);

	i_free(testsuite_env_orig_rcpt_to);

	testsuite_env_orig_rcpt_to = smtp_address_clone(default_pool, address);
	testsuite_rcpt_params.orcpt.addr = testsuite_env_orig_rcpt_to;
}

void testsuite_envelope_set_orig_recipient(const struct sieve_runtime_env *renv,
					   const char *value)
{
	struct smtp_address *address = NULL;
	const char *error;

	if (smtp_address_parse_path(pool_datastack_create(), value,
				    (SMTP_ADDRESS_PARSE_FLAG_ALLOW_LOCALPART |
				     SMTP_ADDRESS_PARSE_FLAG_BRACKETS_OPTIONAL),
				    &address, &error) < 0) {
		e_error(testsuite_sieve_instance->event,
			"testsuite: envelope recipient address "
			"'%s' is invalid: %s", value, error);
	}
	testsuite_envelope_set_orig_recipient_address(renv, address);
}

void testsuite_envelope_set_auth_user(const struct sieve_runtime_env *renv,
				      const char *value)
{
	sieve_message_context_reset(renv->msgctx);

	i_free(testsuite_env_auth);

	testsuite_env_auth = i_strdup(value);
	testsuite_msgdata.auth_user = testsuite_env_auth;
}
