/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "ostream.h"
#include "unlink-directory.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-interpreter.h"

#include "testsuite-message.h"
#include "testsuite-common.h"
#include "testsuite-smtp.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

struct testsuite_smtp_message {
	const struct smtp_address *envelope_from, *envelope_to;
	const char *file;
};

static pool_t testsuite_smtp_pool;
static const char *testsuite_smtp_tmp;
static ARRAY(struct testsuite_smtp_message) testsuite_smtp_messages;

/*
 * Initialize
 */

void testsuite_smtp_init(void)
{
	pool_t pool;

	testsuite_smtp_pool = pool = pool_alloconly_create("testsuite_smtp", 8192);

	testsuite_smtp_tmp = p_strconcat
		(pool, testsuite_tmp_dir_get(), "/smtp", NULL);

	if ( mkdir(testsuite_smtp_tmp, 0700) < 0 ) {
		i_fatal("failed to create temporary directory '%s': %m.",
			testsuite_smtp_tmp);
	}

	p_array_init(&testsuite_smtp_messages, pool, 16);
}

void testsuite_smtp_deinit(void)
{
	const char *error;

	if ( unlink_directory(testsuite_smtp_tmp, UNLINK_DIRECTORY_FLAG_RMDIR, &error) < 0 )
		i_warning("failed to remove temporary directory '%s': %s.",
			testsuite_smtp_tmp, error);

	pool_unref(&testsuite_smtp_pool);
}

void testsuite_smtp_reset(void)
{
	testsuite_smtp_deinit();
	testsuite_smtp_init();
}

/*
 * Simulated SMTP out
 */

struct testsuite_smtp {
	char *msg_file;
	struct smtp_address *mail_from;
	struct ostream *output;
};

void *testsuite_smtp_start
(const struct sieve_script_env *senv ATTR_UNUSED,
	const struct smtp_address *mail_from)
{
	struct testsuite_smtp *smtp;
	unsigned int smtp_count = array_count(&testsuite_smtp_messages);
	int fd;

	smtp = i_new(struct testsuite_smtp, 1);

	smtp->msg_file = i_strdup_printf("%s/%d.eml", testsuite_smtp_tmp, smtp_count);
	smtp->mail_from = smtp_address_clone(default_pool, mail_from);
	
	if ( (fd=open(smtp->msg_file, O_WRONLY | O_CREAT, 0600)) < 0 ) {
		i_fatal("failed create tmp file for SMTP simulation: open(%s) failed: %m",
			smtp->msg_file);
	}

	smtp->output = o_stream_create_fd_autoclose(&fd, (size_t)-1);

	return (void *) smtp;
}

void testsuite_smtp_add_rcpt
(const struct sieve_script_env *senv ATTR_UNUSED,
	void *handle, const struct smtp_address *rcpt_to)
{
	struct testsuite_smtp *smtp = (struct testsuite_smtp *) handle;
	struct testsuite_smtp_message *msg;

	msg = array_append_space(&testsuite_smtp_messages);

	msg->file = p_strdup(testsuite_smtp_pool, smtp->msg_file);
	msg->envelope_from = smtp_address_clone(testsuite_smtp_pool, smtp->mail_from);
	msg->envelope_to = smtp_address_clone(testsuite_smtp_pool, rcpt_to);
}

struct ostream *testsuite_smtp_send
(const struct sieve_script_env *senv ATTR_UNUSED, void *handle)
{
	struct testsuite_smtp *smtp = (struct testsuite_smtp *) handle;

	return smtp->output;
}

void testsuite_smtp_abort
(const struct sieve_script_env *senv ATTR_UNUSED,
	void *handle)
{
	struct testsuite_smtp *smtp = (struct testsuite_smtp *) handle;

	o_stream_ignore_last_errors(smtp->output);
	o_stream_unref(&smtp->output);
	i_unlink(smtp->msg_file);
	i_free(smtp->msg_file);
	i_free(smtp->mail_from);
	i_free(smtp);
}

int testsuite_smtp_finish
(const struct sieve_script_env *senv ATTR_UNUSED,
	void *handle, const char **error_r ATTR_UNUSED)
{
	struct testsuite_smtp *smtp = (struct testsuite_smtp *) handle;
	int ret = 1;

	if (o_stream_finish(smtp->output) < 0) {
		i_error("write(%s) failed: %s", smtp->msg_file,
			o_stream_get_error(smtp->output));
		ret = -1;
	}
	o_stream_unref(&smtp->output);
	i_free(smtp->msg_file);
	i_free(smtp->mail_from);
	i_free(smtp);
	return ret;
}

/*
 * Access
 */

bool testsuite_smtp_get
(const struct sieve_runtime_env *renv, unsigned int index)
{
	const struct testsuite_smtp_message *smtp_msg;

	if ( index >= array_count(&testsuite_smtp_messages) )
		return FALSE;

	smtp_msg = array_idx(&testsuite_smtp_messages, index);

	testsuite_message_set_file(renv, smtp_msg->file);
	testsuite_envelope_set_sender_address(renv, smtp_msg->envelope_from);
	testsuite_envelope_set_recipient_address(renv, smtp_msg->envelope_to);

	return TRUE;
}
