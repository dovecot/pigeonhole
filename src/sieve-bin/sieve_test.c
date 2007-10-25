/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

/* This file was gratefully stolen from dovecot/src/deliver/deliver.c and altered
 * to suit our needs. So, this contains lots and lots of duplicated code. 
 * The sieve_test program needs to read an email message from stdin and it needs 
 * to build a struct mail (to be fed to the sieve library). Deliver does something
 * similar already, so that source was a basis for this test binary. 
 */

#include "lib.h"
#include "lib-signals.h"
#include "file-lock.h"
#include "array.h"
#include "ioloop.h"
#include "hostpid.h"
#include "home-expand.h"
#include "env-util.h"
#include "fd-set-nonblock.h"
#include "istream.h"
#include "istream-seekable.h"
#include "str.h"
#include "str-sanitize.h"
#include "strescape.h"
#include "message-address.h"
#include "message-header-parser.h"
#include "istream-header-filter.h"
#include "mbox-storage.h"
#include "mail-namespace.h"
#include "mbox-from.h"

#include "sieve.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <syslog.h>

#define DEFAULT_AUTH_SOCKET_PATH PKG_RUNDIR"/auth-master"
#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"
#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

/* Hideous .... */

extern struct mail_storage mbox_storage;
void mail_storage_register_all(void) {
	mail_storage_class_register(&mbox_storage);
}

extern struct mailbox_list fs_mailbox_list;
void index_mailbox_list_init(void);
void mailbox_list_register_all(void) {
	mailbox_list_register(&fs_mailbox_list);
	index_mailbox_list_init();
}

/* After buffer grows larger than this, create a temporary file to /tmp
   where to read the mail. */
#define MAIL_MAX_MEMORY_BUFFER (1024*128)

/* FIXME: these two should be in some context struct instead of as globals.. */
static const char *default_mailbox_name = NULL;
static const char *explicit_envelope_sender = NULL;

static struct ioloop *ioloop;

static void sig_die(int signo, void *context ATTR_UNUSED)
{
	/* warn about being killed because of some signal, except SIGINT (^C)
	   which is too common at least while testing :) */
	if (signo != SIGINT)
		i_warning("Killed with signal %d", signo);
	io_loop_stop(ioloop);
}

static int sync_quick(struct mailbox *box)
{
	struct mailbox_sync_context *ctx;
        struct mailbox_sync_rec sync_rec;

	ctx = mailbox_sync_init(box, 0);
	while (mailbox_sync_next(ctx, &sync_rec))
		;
	return mailbox_sync_deinit(&ctx, 0, NULL);
}

const char *deliver_get_return_address(struct mail *mail)
{
	struct message_address *addr;
	const char *str;

	if (explicit_envelope_sender != NULL)
		return explicit_envelope_sender;

	if (mail_get_first_header(mail, "Return-Path", &str) <= 0)
		return NULL;
	addr = message_address_parse(pool_datastack_create(),
				     (const unsigned char *)str,
				     strlen(str), 1, FALSE);
	return addr == NULL || addr->mailbox == NULL || addr->domain == NULL ||
		*addr->mailbox == '\0' || *addr->domain == '\0' ?
		NULL : t_strconcat(addr->mailbox, "@", addr->domain, NULL);
}

const char *deliver_get_new_message_id(void)
{
	static int count = 0;

	return t_strdup_printf("<dovecot-%s-%s-%d@%s>",
			       dec2str(ioloop_timeval.tv_sec),
			       dec2str(ioloop_timeval.tv_usec),
			       count++, "localhost");
}

static const char *address_sanitize(const char *address)
{
	struct message_address *addr;
	const char *ret;
	pool_t pool;

	pool = pool_alloconly_create("address sanitizer", 256);
	addr = message_address_parse(pool, (const unsigned char *)address,
				     strlen(address), 1, FALSE);

	if (addr == NULL || addr->mailbox == NULL || addr->domain == NULL ||
	    *addr->mailbox == '\0')
		ret = DEFAULT_ENVELOPE_SENDER;
	else if (*addr->domain == '\0')
		ret = t_strdup(addr->mailbox);
	else
		ret = t_strdup_printf("%s@%s", addr->mailbox, addr->domain);
	pool_unref(&pool);
	return ret;
}


static void save_header_callback(struct message_header_line *hdr,
				 bool *matched, bool *first)
{
	if (*first) {
		*first = FALSE;
		if (hdr != NULL && strncmp(hdr->name, "From ", 5) == 0)
			*matched = TRUE;
	}
}

static struct istream *
create_mbox_stream(int fd, const char *envelope_sender, bool **first_r)
{
	const char *mbox_hdr;
	struct istream *input_list[4], *input, *input_filter;

	fd_set_nonblock(fd, FALSE);

	envelope_sender = address_sanitize(envelope_sender);
	mbox_hdr = mbox_from_create(envelope_sender, ioloop_time);

	/* kind of kludgy to allocate memory just for this, but since this
	   has to live as long as the input stream itself, this is the safest
	   way to do it without it breaking accidentally. */
	*first_r = i_new(bool, 1);
	**first_r = TRUE;
	input = i_stream_create_fd(fd, 4096, FALSE);
	input_filter =
		i_stream_create_header_filter(input,
					      HEADER_FILTER_EXCLUDE |
					      HEADER_FILTER_NO_CR,
					      mbox_hide_headers,
					      mbox_hide_headers_count,
					      save_header_callback,
					      *first_r);
	i_stream_unref(&input);

	input_list[0] = i_stream_create_from_data(mbox_hdr, strlen(mbox_hdr));
	input_list[1] = input_filter;
	input_list[2] = i_stream_create_from_data("\n", 1);
	input_list[3] = NULL;

	input = i_stream_create_seekable(input_list, MAIL_MAX_MEMORY_BUFFER,
					 "/tmp/dovecot.deliver.");
	i_stream_unref(&input_list[0]);
	i_stream_unref(&input_list[1]);
	i_stream_unref(&input_list[2]);
	return input;
}

void mail_test(struct mail *mail)
{
	const char *const *headers;

	printf("HEADERS\n");
    if (mail_get_headers_utf8(mail, "from", &headers) >= 0)
	{
		printf("HEADERS FOUND\n");	
		int i;
		for ( i = 0; headers[i] != NULL; i++ ) {
			printf("HEADER: From: %s\n", headers[i]);
        } 
	}
}

int main(void)
{
	const char *envelope_sender = DEFAULT_ENVELOPE_SENDER;
	const char *mailbox = "INBOX";
	const char *user, *error;
	struct mail_namespace *mbox_ns;
	struct mail_storage *storage;
	struct mailbox *box;
	struct istream *input;
	struct mailbox_transaction_context *t;
	struct mail *mail;
	struct passwd *pw;
	uid_t process_euid;
	pool_t namespace_pool;
	bool *input_first;

	lib_init();
	ioloop = io_loop_create();

	lib_signals_init();
	lib_signals_set_handler(SIGINT, TRUE, sig_die, NULL);
	lib_signals_set_handler(SIGTERM, TRUE, sig_die, NULL);
	lib_signals_ignore(SIGPIPE, TRUE);
	lib_signals_ignore(SIGALRM, FALSE);

	/* we're non-root. get our username and possibly our home. */
	process_euid = geteuid();
	pw = getpwuid(process_euid);
	if (pw != NULL) {
		user = t_strdup(pw->pw_name);
 	} else {
		i_fatal("Couldn't lookup our username (uid=%s)",
			dec2str(process_euid));
    }

	mail_storage_init();
	mail_storage_register_all();
	mailbox_list_register_all();
	
	namespace_pool = pool_alloconly_create("namespaces", 1024);

	mbox_ns = mail_namespaces_init_empty(namespace_pool);
	mbox_ns->flags |= NAMESPACE_FLAG_INTERNAL;
	if (mail_storage_create(mbox_ns, "mbox", "/tmp", user,
				0, FILE_LOCK_METHOD_FCNTL, &error) < 0)
		i_fatal("Couldn't create internal mbox storage: %s", error);
	input = create_mbox_stream(0, envelope_sender, &input_first);
	box = mailbox_open(mbox_ns->storage, "Dovecot Delivery Mail", input,
			   MAILBOX_OPEN_NO_INDEX_FILES |
			   MAILBOX_OPEN_MBOX_ONE_MSG_ONLY);
	if (box == NULL)
		i_fatal("Can't open delivery mail as mbox");
        if (sync_quick(box) < 0)
		i_fatal("Can't sync delivery mail");

	t = mailbox_transaction_begin(box, 0);
	mail = mail_alloc(t, 0, NULL);
	mail_set_seq(mail, 1);

	storage = NULL;
	default_mailbox_name = mailbox;

	/* */
	i_stream_seek(input, 0);
	mail_test(mail);
	//ret = deliver_save(ns, &storage, mailbox, mail, 0, NULL);

	i_stream_unref(&input);
	i_free(input_first);

	mail_free(&mail);
	mailbox_transaction_rollback(&t);
	mailbox_close(&box);

	mail_namespaces_deinit(&mbox_ns);

	mail_storage_deinit();

	lib_signals_deinit();

	io_loop_destroy(&ioloop);
	lib_deinit();
  
  return 0;
}
