/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __MAIL_RAW_H
#define __MAIL_RAW_H

struct mail_raw {
	pool_t pool;
	struct mail *mail;

	struct mailbox *box;
	struct mailbox_header_lookup_ctx *headers_ctx;
	struct mailbox_transaction_context *trans;
};

void mail_raw_init(const char *user);
void mail_raw_deinit(void);

struct mail_raw *mail_raw_open_file(const char *path);
struct mail_raw *mail_raw_open_data(string_t *mail_data);
void mail_raw_close(struct mail_raw *mailr);


#endif /* __MAIL_RAW_H */
