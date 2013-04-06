/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "istream.h"
#include "istream-seekable.h"
#include "fd-set-nonblock.h"
#include "str.h"
#include "str-sanitize.h"
#include "strescape.h"
#include "safe-mkstemp.h"
#include "mkdir-parents.h"
#include "abspath.h"
#include "message-address.h"
#include "mbox-from.h"
#include "raw-storage.h"
#include "mail-namespace.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "settings-parser.h"
#include "mail-raw.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

/*
 * Configuration
 */

#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

/* After buffer grows larger than this, create a temporary file to /tmp
   where to read the mail. */
#define MAIL_MAX_MEMORY_BUFFER (1024*128)

static const char *wanted_headers[] = {
	"From", "Message-ID", "Subject", "Return-Path",
	NULL
};

/*
 * Global data
 */

struct mail_raw_user {
	struct mail_namespace *ns;
	struct mail_user *mail_user;
};

/*
 * Raw mail implementation
 */

static int seekable_fd_callback
(const char **path_r, void *context)
{
	struct mail_user *ruser = (struct mail_user *)context;
	const char *dir, *p;
	string_t *path;
	int fd;

	path = t_str_new(128);
	mail_user_set_get_temp_prefix(path, ruser->set);
	fd = safe_mkstemp(path, 0600, (uid_t)-1, (gid_t)-1);
	if (fd == -1 && errno == ENOENT) {
		dir = str_c(path);
		p = strrchr(dir, '/');
		if (p != NULL) {
			dir = t_strdup_until(dir, p);
			if ( mkdir_parents(dir, 0600) < 0 ) {
				i_error("mkdir_parents(%s) failed: %m", dir);
				return -1;
			}
			fd = safe_mkstemp(path, 0600, (uid_t)-1, (gid_t)-1);
		}
	}

	if (fd == -1) {
		i_error("safe_mkstemp(%s) failed: %m", str_c(path));
		return -1;
	}

	/* we just want the fd, unlink it */
	if (unlink(str_c(path)) < 0) {
		/* shouldn't happen.. */
		i_error("unlink(%s) failed: %m", str_c(path));
		i_close_fd(&fd);
		return -1;
	}

	*path_r = str_c(path);
	return fd;
}

static struct istream *mail_raw_create_stream
(struct mail_user *ruser, int fd, time_t *mtime_r, const char **sender)
{
	struct istream *input, *input2, *input_list[2];
	const unsigned char *data;
	size_t i, size;
	int ret, tz;
	char *env_sender = NULL;

	*mtime_r = (time_t)-1;
	fd_set_nonblock(fd, FALSE);

	input = i_stream_create_fd(fd, 4096, FALSE);
	input->blocking = TRUE;
	/* If input begins with a From-line, drop it */
	ret = i_stream_read_data(input, &data, &size, 5);
	if (ret > 0 && size >= 5 && memcmp(data, "From ", 5) == 0) {
		/* skip until the first LF */
		i_stream_skip(input, 5);
		while ( i_stream_read_data(input, &data, &size, 0) > 0 ) {
			for (i = 0; i < size; i++) {
				if (data[i] == '\n')
					break;
			}
			if (i != size) {
				(void)mbox_from_parse(data, i, mtime_r, &tz, &env_sender);
				i_stream_skip(input, i + 1);
				break;
			}
			i_stream_skip(input, size);
		}
	}

	if (env_sender != NULL && sender != NULL) {
		*sender = t_strdup(env_sender);
	}
	i_free(env_sender);

	if (input->v_offset == 0) {
		input2 = input;
		i_stream_ref(input2);
	} else {
		input2 = i_stream_create_limit(input, (uoff_t)-1);
	}
	i_stream_unref(&input);

	input_list[0] = input2; input_list[1] = NULL;
	input = i_stream_create_seekable(input_list, MAIL_MAX_MEMORY_BUFFER,
		seekable_fd_callback, (void*)ruser);
	i_stream_unref(&input2);
	return input;
}

/*
 * Init/Deinit
 */

struct mail_user *mail_raw_user_create
(struct master_service *service, struct mail_user *mail_user)
{
	void **sets = master_service_settings_get_others(service);

	return raw_storage_create_from_set(mail_user->set_info, sets[0]);
}

/*
 * Open raw mail data
 */

static struct mail_raw *mail_raw_create
(struct mail_user *ruser, struct istream *input,
	const char *mailfile, const char *sender, time_t mtime)
{
	struct mail_raw *mailr;
	struct mailbox_header_lookup_ctx *headers_ctx;
	const char *envelope_sender;
	int ret;

	if ( mailfile != NULL && *mailfile != '/' )
		mailfile = t_abspath(mailfile);

	mailr = i_new(struct mail_raw, 1);

	envelope_sender = sender != NULL ? sender : DEFAULT_ENVELOPE_SENDER;
	if ( mailfile == NULL ) {
		ret = raw_mailbox_alloc_stream(ruser, input, mtime,
					       envelope_sender, &mailr->box);
	} else {
		ret = raw_mailbox_alloc_path(ruser, mailfile, (time_t)-1,
					     envelope_sender, &mailr->box);
	}

	if ( ret < 0 ) {
		if ( mailfile == NULL ) {
			i_fatal("Can't open delivery mail as raw: %s",
				mailbox_get_last_error(mailr->box, NULL));
		} else {
			i_fatal("Can't open delivery mail as raw (file=%s): %s",
				mailfile, mailbox_get_last_error(mailr->box, NULL));
		}
	}

	mailr->trans = mailbox_transaction_begin(mailr->box, 0);
	headers_ctx = mailbox_header_lookup_init(mailr->box, wanted_headers);
	mailr->mail = mail_alloc(mailr->trans, 0, headers_ctx);
	mailbox_header_lookup_unref(&headers_ctx);
	mail_set_seq(mailr->mail, 1);

	return mailr;
}

struct mail_raw *mail_raw_open_data
(struct mail_user *ruser, string_t *mail_data)
{
	struct mail_raw *mailr;
	struct istream *input;

	input = i_stream_create_from_data(str_data(mail_data), str_len(mail_data));
	i_stream_set_name(input, "data");

	mailr = mail_raw_create(ruser, input, NULL, NULL, (time_t)-1);

	i_stream_unref(&input);

	return mailr;
}

struct mail_raw *mail_raw_open_file
(struct mail_user *ruser, const char *path)
{
	struct mail_raw *mailr;
	struct istream *input = NULL;
	time_t mtime = (time_t)-1;
	const char *sender = NULL;

	if ( path == NULL || strcmp(path, "-") == 0 ) {
		path = NULL;
		input = mail_raw_create_stream(ruser, 0, &mtime, &sender);
	}

	mailr = mail_raw_create(ruser, input, path, sender, mtime);

	if ( input != NULL )
		i_stream_unref(&input);

	return mailr;
}

void mail_raw_close(struct mail_raw **mailr)
{
	mail_free(&(*mailr)->mail);
	mailbox_transaction_rollback(&(*mailr)->trans);
	mailbox_free(&(*mailr)->box);

	i_free(*mailr);
	*mailr = NULL;
}

