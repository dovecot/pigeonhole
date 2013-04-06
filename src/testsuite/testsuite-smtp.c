/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
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

#include <sys/stat.h>
#include <sys/types.h>

struct testsuite_smtp_message {
	const char *envelope_from;
	const char *envelope_to;
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
	if ( unlink_directory(testsuite_smtp_tmp, TRUE) < 0 )
		i_warning("failed to remove temporary directory '%s': %m.",
			testsuite_smtp_tmp);

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
	const char *tmp_path;
	struct ostream *output;
};

void *testsuite_smtp_open
(const struct sieve_script_env *senv ATTR_UNUSED, const char *destination,
	const char *return_path, struct ostream **output_r)
{
	struct testsuite_smtp_message smtp_msg;
	struct testsuite_smtp *smtp;
	unsigned int smtp_count = array_count(&testsuite_smtp_messages);
	int fd;

	smtp_msg.file = p_strdup_printf(testsuite_smtp_pool,
		"%s/%d.eml", testsuite_smtp_tmp, smtp_count);
	smtp_msg.envelope_from =
		( return_path != NULL ? p_strdup(testsuite_smtp_pool, return_path) : NULL );
	smtp_msg.envelope_to = p_strdup(testsuite_smtp_pool, destination);

	array_append(&testsuite_smtp_messages, &smtp_msg, 1);

	smtp = t_new(struct testsuite_smtp, 1);
	smtp->tmp_path = smtp_msg.file;

	if ( (fd=open(smtp->tmp_path, O_WRONLY | O_CREAT, 0600)) < 0 ) {
		i_fatal("failed create tmp file for SMTP simulation: open(%s) failed: %m",
			smtp->tmp_path);
	}

	smtp->output = o_stream_create_fd(fd, (size_t)-1, TRUE);
	*output_r = smtp->output;

	return (void *) smtp;
}

bool testsuite_smtp_close
(const struct sieve_script_env *senv ATTR_UNUSED, void *handle)
{
	struct testsuite_smtp *smtp = (struct testsuite_smtp *) handle;

	o_stream_unref(&smtp->output);
	return TRUE;
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
	testsuite_envelope_set_sender(renv, smtp_msg->envelope_from);
	testsuite_envelope_set_recipient(renv, smtp_msg->envelope_to);

	return TRUE;
}
