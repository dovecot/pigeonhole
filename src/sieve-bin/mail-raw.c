/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

/* This file was gratefully stolen from dovecot/src/deliver/deliver.c and altered
 * to suit our needs. So, this contains lots and lots of duplicated code. 
 * The sieve_test program needs to read an email message from stdin and it needs 
 * to build a struct mail (to be fed to the sieve library). Deliver does something
 * similar already, so that source was a basis for this test binary. 
 */

#include "lib.h"
#include "istream.h"
#include "istream-seekable.h"
#include "fd-set-nonblock.h"
#include "str.h"
#include "str-sanitize.h"
#include "strescape.h"
#include "message-address.h"
#include "raw-storage.h"
#include "mail-namespace.h"

#include "mail-raw.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

/* After buffer grows larger than this, create a temporary file to /tmp
   where to read the mail. */
#define MAIL_MAX_MEMORY_BUFFER (1024*128)

static struct istream *create_raw_stream(int fd)
{
	struct istream *input, *input2, *input_list[2];
	const unsigned char *data;
	size_t i, size;
	int ret;

	fd_set_nonblock(fd, FALSE);

	input = i_stream_create_fd(fd, 4096, FALSE);
	input->blocking = TRUE;
	/* If input begins with a From-line, drop it */
	ret = i_stream_read_data(input, &data, &size, 5);
	if (ret > 0 && size >= 5 && memcmp(data, "From ", 5) == 0) {
		/* skip until the first LF */
		i_stream_skip(input, 5);
		while ((ret = i_stream_read_data(input, &data, &size, 0)) > 0) {
			for (i = 0; i < size; i++) {
				if (data[i] == '\n')
					break;
			}
			if (i != size) {
				i_stream_skip(input, i + 1);
				break;
			}
			i_stream_skip(input, size);
 		}
	}

	if (input->v_offset == 0) {
		input2 = input;
		i_stream_ref(input2);
	} else {
		input2 = i_stream_create_limit(input, (uoff_t)-1);
	}
	i_stream_unref(&input);

	input_list[0] = input2; input_list[1] = NULL;
	input = i_stream_create_seekable(input_list, MAIL_MAX_MEMORY_BUFFER,
		"/tmp/dovecot.deliver.");
	i_stream_unref(&input2);
	return input;
}

static struct mail_namespace *raw_ns;

void mail_raw_init(pool_t namespaces_pool, const char *user) 
{
	const char *error;

	raw_ns = mail_namespaces_init_empty(namespaces_pool);
	raw_ns->flags |= NAMESPACE_FLAG_INTERNAL;
	if (mail_storage_create(raw_ns, "raw", "/tmp", user,
				0, FILE_LOCK_METHOD_FCNTL, &error) < 0)
		i_fatal("Couldn't create internal raw storage: %s", error);	
}	
	
struct mail_raw *mail_raw_open(int fd)
{
	pool_t pool;
	struct raw_mailbox *raw_box;
	struct mail_raw *mailr;
	
	pool = pool_alloconly_create("mail_raw", 1024);
	mailr = p_new(pool, struct mail_raw, 1);
	mailr->pool = pool;

	mailr->input = create_raw_stream(fd);

	mailr->box = mailbox_open(raw_ns->storage, 
		"Dovecot Raw Mail", mailr->input, MAILBOX_OPEN_NO_INDEX_FILES);
	
	if (mailr->box == NULL)
		i_fatal("Can't open mail stream as raw");

	if (mailbox_sync(mailr->box, 0, 0, NULL) < 0) {
		enum mail_error error;

		i_fatal("Can't sync raw mail: %s",
		mail_storage_get_last_error(raw_ns->storage, &error));
	}
    raw_box = (struct raw_mailbox *) mailr->box;
    raw_box->envelope_sender = DEFAULT_ENVELOPE_SENDER;

	mailr->trans = mailbox_transaction_begin(mailr->box, 0);
	mailr->mail = mail_alloc(mailr->trans, 0, NULL);
	mail_set_seq(mailr->mail, 1);

	/* */
	i_stream_seek(mailr->input, 0);

	return mailr;
}

void mail_raw_close(struct mail_raw *mailr) 
{
	i_stream_unref(&mailr->input);

	mail_free(&mailr->mail);
	mailbox_transaction_rollback(&mailr->trans);
	mailbox_close(&mailr->box);

	pool_unref(&mailr->pool);
}

void mail_raw_deinit(void)
{
	mail_namespaces_deinit(&raw_ns);
}

