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
#include "raw-storage.h"
#include "mail-namespace.h"

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

extern struct mail_storage raw_storage;
void mail_storage_register_all(void) {
	mail_storage_class_register(&raw_storage);
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

static struct ioloop *ioloop;

static void sig_die(int signo, void *context ATTR_UNUSED)
{
	/* warn about being killed because of some signal, except SIGINT (^C)
	   which is too common at least while testing :) */
	if (signo != SIGINT)
		i_warning("Killed with signal %d", signo);
	io_loop_stop(ioloop);
}

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
		input2 = i_stream_create_limit(input, input->v_offset,
			(uoff_t)-1);
	}
	i_stream_unref(&input);

	input_list[0] = input2; input_list[1] = NULL;
	input = i_stream_create_seekable(input_list, MAIL_MAX_MEMORY_BUFFER,
		"/tmp/dovecot.deliver.");
	i_stream_unref(&input2);
	return input;
}

static void sieve_test(struct sieve_binary *sbin, struct mail *mail)
{
	const char *const *headers;

	printf("HEADERS\n");
	if (mail_get_headers_utf8(mail, "from", &headers) >= 0) {	
		int i;
		for ( i = 0; headers[i] != NULL; i++ ) {
			printf("HEADER: From: %s\n", headers[i]);
		} 
	}
	
	sieve_execute(sbin, mail);
}

int main(int argc, char **argv) 
{
	const char *envelope_sender = DEFAULT_ENVELOPE_SENDER;
	const char *mailbox = "INBOX";
	const char *user, *error;
	struct mail_namespace *raw_ns;
	struct mail_storage *storage;
	struct mailbox *box;
	struct raw_mailbox *raw_box;
	struct istream *input;
	struct mailbox_transaction_context *t;
	struct mail *mail;
	struct passwd *pw;
	uid_t process_euid;
	pool_t namespace_pool;
	int fd;
	struct sieve_binary *sbin;

	lib_init();
	ioloop = io_loop_create();

	lib_signals_init();
	lib_signals_set_handler(SIGINT, TRUE, sig_die, NULL);
	lib_signals_set_handler(SIGTERM, TRUE, sig_die, NULL);
	lib_signals_ignore(SIGPIPE, TRUE);
	lib_signals_ignore(SIGALRM, FALSE);
		
	if ( argc < 2 ) {
		printf( "Usage: sieve_test <filename>\n");
 		exit(1);
 	}
  
  /* Compile sieve script */
  
	if ( (fd = open(argv[1], O_RDONLY)) < 0 ) {
		perror("open()");
		exit(1);
	}

	printf("Parsing sieve script '%s'...\n", argv[1]);

	if ( (sbin = sieve_compile(fd)) == NULL ) 
		exit(1);
	
	(void) sieve_dump(sbin);

 	close(fd);

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

	raw_ns = mail_namespaces_init_empty(namespace_pool);
	raw_ns->flags |= NAMESPACE_FLAG_INTERNAL;
	if (mail_storage_create(raw_ns, "raw", "/tmp", user,
				0, FILE_LOCK_METHOD_FCNTL, &error) < 0)
		i_fatal("Couldn't create internal raw storage: %s", error);
	input = create_raw_stream(0);
	box = mailbox_open(raw_ns->storage, "Dovecot Delivery Mail", input,
			   MAILBOX_OPEN_NO_INDEX_FILES);
	if (box == NULL)
		i_fatal("Can't open delivery mail as raw");

	if (mailbox_sync(box, 0, 0, NULL) < 0) {
		enum mail_error error;

		i_fatal("Can't sync delivery mail: %s",
		mail_storage_get_last_error(raw_ns->storage, &error));
	}
    raw_box = (struct raw_mailbox *)box;
    raw_box->envelope_sender = envelope_sender;

	t = mailbox_transaction_begin(box, 0);
	mail = mail_alloc(t, 0, NULL);
	mail_set_seq(mail, 1);

	storage = NULL;
	default_mailbox_name = mailbox;

	/* */
	i_stream_seek(input, 0);
	sieve_test(sbin, mail);
	//ret = deliver_save(ns, &storage, mailbox, mail, 0, NULL);

	i_stream_unref(&input);

	mail_free(&mail);
	mailbox_transaction_rollback(&t);
	mailbox_close(&box);

	mail_namespaces_deinit(&raw_ns);

	mail_storage_deinit();

	lib_signals_deinit();

	io_loop_destroy(&ioloop);
	lib_deinit();
  
  return 0;
}
