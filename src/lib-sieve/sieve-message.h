/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_MESSAGE_H
#define __SIEVE_MESSAGE_H

/*
 * Message transmission
 */

const char *sieve_message_get_new_id(const struct sieve_instance *svinst);

/*
 * Message context
 */

struct sieve_message_context;

struct sieve_message_context *sieve_message_context_create
	(struct sieve_instance *svinst, struct mail_user *mail_user,
		const struct sieve_message_data *msgdata);
void sieve_message_context_ref(struct sieve_message_context *msgctx);
void sieve_message_context_unref(struct sieve_message_context **msgctx);

void sieve_message_context_reset(struct sieve_message_context *msgctx);

pool_t sieve_message_context_pool
	(struct sieve_message_context *msgctx);

/* Extension support */

void sieve_message_context_extension_set
	(struct sieve_message_context *msgctx, const struct sieve_extension *ext,
		void *context);
const void *sieve_message_context_extension_get
	(struct sieve_message_context *msgctx, const struct sieve_extension *ext);

/* Envelope */

const struct sieve_address *sieve_message_get_final_recipient_address
	(struct sieve_message_context *msgctx);
const struct sieve_address *sieve_message_get_orig_recipient_address
	(struct sieve_message_context *msgctx);

const struct sieve_address *sieve_message_get_sender_address
	(struct sieve_message_context *msgctx);

const char *sieve_message_get_orig_recipient
	(struct sieve_message_context *msgctx);
const char *sieve_message_get_final_recipient
	(struct sieve_message_context *msgctx);

const char *sieve_message_get_sender
	(struct sieve_message_context *msgctx);

/* Mail */

struct mail *sieve_message_get_mail
	(struct sieve_message_context *msgctx);

int sieve_message_substitute
	(struct sieve_message_context *msgctx, struct istream *input);
struct edit_mail *sieve_message_edit
	(struct sieve_message_context *msgctx);
void sieve_message_snapshot
	(struct sieve_message_context *msgctx);

/*
 * Header stringlist
 */

struct sieve_stringlist *sieve_message_header_stringlist_create
	(const struct sieve_runtime_env *renv, struct sieve_stringlist *field_names,
		bool mime_decode);

#endif /* __SIEVE_MESSAGE_H */
