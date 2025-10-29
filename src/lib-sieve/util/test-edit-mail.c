/* Copyright (c) 2018 Pigeonhole authors, see the included COPYING file */

#include "lib.h"
#include "test-common.h"
#include "test-dir.h"
#include "path-util.h"
#include "buffer.h"
#include "str.h"
#include "istream.h"
#include "istream-concat.h"
#include "istream-crlf.h"
#include "unlink-directory.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "istream-header-filter.h"
#include "mail-storage.h"
#include "mail-storage-service.h"
#include "mail-user.h"

#include "mail-raw.h"
#include "edit-mail.h"

#include <time.h>

static pool_t test_pool;

static struct mail_storage_service_ctx *mail_storage_service = NULL;
static struct mail_user *test_mail_user = NULL;
static struct mail_storage_service_user *test_service_user = NULL;
static const char *mail_home;

static struct mail_user *test_raw_mail_user = NULL;

static void str_append_no_cr(string_t *str, const char *cstr)
{
	const char *p, *poff;

	poff = p = cstr;
	while (*p != '\0') {
		if (*p == '\r') {
			str_append_data(str, poff, (p - poff));
			poff = p+1;
		}
		p++;
	}
	str_append_data(str, poff, (p - poff));
}

static int test_init_mail_user(void)
{
	const char *error;

	mail_home = p_strdup_printf(test_pool, "%s/test_user.%ld.%ld",
				    test_dir_get(), (long)time(NULL), (long)getpid());

	struct mail_storage_service_input input = {
		.userdb_fields = (const char*const[]){
			"mail_driver=maildir",
			"mail_path=~/",
			t_strdup_printf("home=%s", mail_home),
			NULL
		},
		.username = "test@example.com",
		.no_userdb_lookup = TRUE,
		.debug = TRUE,
	};

	mail_storage_service = mail_storage_service_init(
		master_service,
		(MAIL_STORAGE_SERVICE_FLAG_NO_RESTRICT_ACCESS |
		 MAIL_STORAGE_SERVICE_FLAG_NO_LOG_INIT |
		 MAIL_STORAGE_SERVICE_FLAG_NO_PLUGINS));

	if (mail_storage_service_lookup(mail_storage_service, &input,
					&test_service_user, &error) < 0)
	{
		i_error("Cannot lookup test user: %s", error);
		return -1;
	}

	if (mail_storage_service_next(mail_storage_service, test_service_user,
				      &test_mail_user, &error) < 0)
	{
		 i_error("Cannot lookup test user: %s", error);
		 return -1;
	}

	return 0;
}

static void test_deinit_mail_user()
{
	const char *error;
	mail_user_unref(&test_mail_user);
	mail_storage_service_user_unref(&test_service_user);
	mail_storage_service_deinit(&mail_storage_service);
	if (unlink_directory(mail_home, UNLINK_DIRECTORY_FLAG_RMDIR,
			     &error) < 0)
		i_error("unlink_directory(%s) failed: %s", mail_home, error);
}

static void test_edit_mail_init(void)
{
	test_pool = pool_alloconly_create(MEMPOOL_GROWING"test pool", 128);

	test_init_mail_user();
	test_raw_mail_user = mail_raw_user_create(test_mail_user);
}

static void test_edit_mail_deinit(void)
{
	mail_user_unref(&test_raw_mail_user);
	test_deinit_mail_user();
	pool_unref(&test_pool);
}

static void test_stream_data(struct istream *input, buffer_t *buffer)
{
	const unsigned char *data;
	size_t size;

	while (i_stream_read_more(input, &data, &size) > 0) {
		buffer_append(buffer, data, size);
		i_stream_skip(input, size);
	}

	test_assert(!i_stream_have_bytes_left(input));
	test_assert(input->stream_errno == 0);
}

static void test_stream_data_slow(struct istream *input, buffer_t *buffer)
{
	const unsigned char *data;
	size_t size;
	int ret;

	ret = i_stream_read(input);
	while (ret > 0 || i_stream_have_bytes_left(input) || ret == -2) {
		data = i_stream_get_data(input, &size);
		buffer_append(buffer, data, 1);
		i_stream_skip(input, 1);

		ret = i_stream_read(input);
	}

	test_assert(!i_stream_have_bytes_left(input));
	test_assert(input->stream_errno == 0);
}

static void test_edit_mail_concatenated(void)
{
	static const char *hide_headers[] =
		{ "Return-Path", "X-Sieve", "X-Sieve-Redirected-From" };
	static const char *msg_part1 =
		"Received: from example.com ([127.0.0.1] helo=example.com)\r\n"
		"	by example.org with LMTP (Dovecot)\r\n"
		"	(envelope-from <frop-bounces@example.com>)\r\n"
		"	id 1er3e8-0015df-QO\r\n"
		"	for timo@example.org;\r\n"
		"	Sat, 03 Mar 2018 10:40:05 +0100\r\n";
	static const char *msg_part2 =
		"Return-Path: <stephan@example.com>\r\n";
	static const char *msg_part3 =
		"Delivered-To: <timo@example.org>\r\n";
	static const char *msg_part4 =
		"From: <stephan@example.com>\r\n"
		"To: <timo@example.org>\r\n"
		"Subject: Sieve editheader breaks with LMTP\r\n"
		"\r\n"
		"Hi,\r\n"
		"\r\n"
		"Sieve editheader seems to be broken when used from LMTP\r\n"
		"\r\n"
		"Regards,\r\n"
		"\r\n"
		"Stephan.\r\n";
	static const char *msg_added =
		"X-Filter-Junk-Type: NONE\r\n"
		"X-Filter-Junk-Flag: NO\r\n";
	struct istream *inputs[5], *input_msg, *input_filt, *input_mail, *input;
	buffer_t *buffer;
	struct mail_raw *rawmail;
	struct edit_mail *edmail;
	struct mail *mail;
	string_t *expected;
	const char *value;

	test_begin("edit-mail - concatenated");
	test_edit_mail_init();

	/* Compose the message */

	inputs[0] = i_stream_create_from_data(msg_part1, strlen(msg_part1));
	inputs[1] = i_stream_create_from_data(msg_part2, strlen(msg_part2));
	inputs[2] = i_stream_create_from_data(msg_part3, strlen(msg_part3));
	inputs[3] = i_stream_create_from_data(msg_part4, strlen(msg_part4));
	inputs[4] = NULL;

	input_msg = i_stream_create_concat(inputs);

	i_stream_unref(&inputs[0]);
	i_stream_unref(&inputs[1]);
	i_stream_unref(&inputs[2]);
	i_stream_unref(&inputs[3]);

	rawmail = mail_raw_open_stream(test_raw_mail_user, input_msg);

	/* Add headers */

	edmail = edit_mail_wrap(rawmail->mail);

	edit_mail_header_add(edmail, "X-Filter-Junk-Flag", "NO", FALSE);
	edit_mail_header_add(edmail, "X-Filter-Junk-Type", "NONE", FALSE);

	mail = edit_mail_get_mail(edmail);

	/* Evaluate modified header */

	test_assert(mail_get_first_header_utf8(mail, "Subject", &value) > 0);
	test_assert(strcmp(value, "Sieve editheader breaks with LMTP") == 0);

	test_assert(mail_get_first_header_utf8(mail, "X-Filter-Junk-Flag",
					       &value) > 0);
	test_assert(strcmp(value, "NO") == 0);
	test_assert(mail_get_first_header_utf8(mail, "X-Filter-Junk-Type",
					       &value) > 0);
	test_assert(strcmp(value, "NONE") == 0);

	test_assert(mail_get_first_header_utf8(mail, "Delivered-To",
					       &value) > 0);

	/* Prepare tests */

	if (mail_get_stream(mail, NULL, NULL, &input_mail) < 0) {
		i_fatal("Failed to open mail stream: %s",
			mailbox_get_last_internal_error(mail->box, NULL));
	}

	buffer = buffer_create_dynamic(default_pool, 1024);
	expected = t_str_new(1024);

	/* Added */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);
	input = input_mail;

	test_stream_data(input_mail, buffer);

	str_truncate(expected, 0);
	str_append(expected, msg_added);
	str_append(expected, msg_part1);
	str_append(expected, msg_part2);
	str_append(expected, msg_part3);
	str_append(expected, msg_part4);

	test_out("added", strcmp(str_c(buffer), str_c(expected)) == 0);

	/* Added, slow */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);

	test_stream_data_slow(input_mail, buffer);

	str_truncate(expected, 0);
	str_append(expected, msg_added);
	str_append(expected, msg_part1);
	str_append(expected, msg_part2);
	str_append(expected, msg_part3);
	str_append(expected, msg_part4);

	test_out("added, slow", strcmp(str_c(buffer), str_c(expected)) == 0);

	/* Added, filtered */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);

	input_filt = i_stream_create_header_filter(
		input_mail, (HEADER_FILTER_EXCLUDE | HEADER_FILTER_NO_CR),
		hide_headers, N_ELEMENTS(hide_headers),
		*null_header_filter_callback, NULL);
	input = i_stream_create_lf(input_filt);
	i_stream_unref(&input_filt);

	test_stream_data(input, buffer);
	test_assert(!i_stream_have_bytes_left(input_mail));
	test_assert(input_mail->stream_errno == 0);

	str_truncate(expected, 0);
	str_append_no_cr(expected, msg_added);
	str_append_no_cr(expected, msg_part1);
	str_append_no_cr(expected, msg_part3);
	str_append_no_cr(expected, msg_part4);

	test_out("added, filtered",
		 strcmp(str_c(buffer), str_c(expected)) == 0);

	i_stream_unref(&input);

	/* Added, filtered, slow */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);

	input_filt = i_stream_create_header_filter(
		input_mail, (HEADER_FILTER_EXCLUDE | HEADER_FILTER_NO_CR),
		hide_headers, N_ELEMENTS(hide_headers),
		*null_header_filter_callback, NULL);
	input = i_stream_create_lf(input_filt);
	i_stream_unref(&input_filt);

	test_stream_data_slow(input, buffer);
	test_assert(!i_stream_have_bytes_left(input_mail));
	test_assert(input_mail->stream_errno == 0);

	str_truncate(expected, 0);
	str_append_no_cr(expected, msg_added);
	str_append_no_cr(expected, msg_part1);
	str_append_no_cr(expected, msg_part3);
	str_append_no_cr(expected, msg_part4);

	test_out("added, filtered, slow",
		 strcmp(str_c(buffer), str_c(expected)) == 0);

	i_stream_unref(&input);

	/* Delete header */

	edit_mail_header_delete(edmail, "Delivered-To", 0);

	/* Evaluate modified header */

	test_assert(mail_get_first_header_utf8(mail, "Subject", &value) > 0);
	test_assert(strcmp(value, "Sieve editheader breaks with LMTP") == 0);

	test_assert(mail_get_first_header_utf8(mail, "X-Filter-Junk-Flag",
					       &value) > 0);
	test_assert(strcmp(value, "NO") == 0);
	test_assert(mail_get_first_header_utf8(mail, "X-Filter-Junk-Type",
					       &value) > 0);
	test_assert(strcmp(value, "NONE") == 0);

	test_assert(mail_get_first_header_utf8(mail, "Delivered-To",
					       &value) == 0);

	/* Deleted */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);
	input = input_mail;

	test_stream_data(input_mail, buffer);

	str_truncate(expected, 0);
	str_append(expected, msg_added);
	str_append(expected, msg_part1);
	str_append(expected, msg_part2);
	str_append(expected, msg_part4);

	test_out("deleted", strcmp(str_c(buffer), str_c(expected)) == 0);

	/* Deleted, slow */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);

	test_stream_data_slow(input_mail, buffer);

	str_truncate(expected, 0);
	str_append(expected, msg_added);
	str_append(expected, msg_part1);
	str_append(expected, msg_part2);
	str_append(expected, msg_part4);

	test_out("deleted, slow", strcmp(str_c(buffer), str_c(expected)) == 0);

	/* Deleted, filtered */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);

	input_filt = i_stream_create_header_filter(
		input_mail, (HEADER_FILTER_EXCLUDE | HEADER_FILTER_NO_CR),
		hide_headers, N_ELEMENTS(hide_headers),
		*null_header_filter_callback, NULL);
	input = i_stream_create_lf(input_filt);
	i_stream_unref(&input_filt);

	test_stream_data(input, buffer);
	test_assert(!i_stream_have_bytes_left(input_mail));
	test_assert(input_mail->stream_errno == 0);

	str_truncate(expected, 0);
	str_append_no_cr(expected, msg_added);
	str_append_no_cr(expected, msg_part1);
	str_append_no_cr(expected, msg_part4);

	test_out("deleted, filtered",
		 strcmp(str_c(buffer), str_c(expected)) == 0);

	i_stream_unref(&input);

	/* Deleted, filtered, slow */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);

	input_filt = i_stream_create_header_filter(input_mail,
		HEADER_FILTER_EXCLUDE | HEADER_FILTER_NO_CR, hide_headers,
		N_ELEMENTS(hide_headers), *null_header_filter_callback,
		NULL);
	input = i_stream_create_lf(input_filt);
	i_stream_unref(&input_filt);

	test_stream_data_slow(input, buffer);
	test_assert(!i_stream_have_bytes_left(input_mail));
	test_assert(input_mail->stream_errno == 0);

	str_truncate(expected, 0);
	str_append_no_cr(expected, msg_added);
	str_append_no_cr(expected, msg_part1);
	str_append_no_cr(expected, msg_part4);

	test_out("deleted, filtered, slow",
		 strcmp(str_c(buffer), str_c(expected)) == 0);

	i_stream_unref(&input);

	/* clean up */

	buffer_free(&buffer);
	edit_mail_unwrap(&edmail);
	mail_raw_close(&rawmail);
	i_stream_unref(&input_msg);
	test_edit_mail_deinit();
	test_end();
}

static const char *big_header =
	"X-A: AAAA\n"
	"X-Big-One: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
	" AAAAAAAAAAAAAAAAAAAAAAAAA\n"
	"X-B: BBBB\n"
	"\n"
	"Frop!\n";

static void test_edit_mail_big_header(void)
{
	struct istream *input_msg, *input_mail;
	buffer_t *buffer;
	struct mail_raw *rawmail;
	struct edit_mail *edmail;
	struct mail *mail;
	const char *value;

	test_begin("edit-mail - big header");
	test_edit_mail_init();

	/* compose the message */

	input_msg = i_stream_create_from_data(big_header, strlen(big_header));

	rawmail = mail_raw_open_stream(test_raw_mail_user, input_msg);

	edmail = edit_mail_wrap(rawmail->mail);

	/* delete header */

	edit_mail_header_delete(edmail, "X-B", 0);
	mail = edit_mail_get_mail(edmail);

	/* prepare tests */

	if (mail_get_stream(mail, NULL, NULL, &input_mail) < 0) {
		i_fatal("Failed to open mail stream: %s",
			mailbox_get_last_internal_error(mail->box, NULL));
	}

	buffer = buffer_create_dynamic(default_pool, 1024);

	/* evaluate modified header */

	test_assert(mail_get_first_header_utf8(mail, "X-B", &value) == 0);

	/* deleted */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);

	test_stream_data(input_mail, buffer);

	/* clean up */

	buffer_free(&buffer);
	edit_mail_unwrap(&edmail);
	mail_raw_close(&rawmail);
	i_stream_unref(&input_msg);
	test_edit_mail_deinit();
	test_end();
}

static void test_edit_mail_small_buffer(void)
{
	static const char *message =
		"X-A: AAAA\n"
		"X-B: BBBB\n"
		"\n"
		"Frop!\n";
	struct istream *input_msg, *input_mail;
	buffer_t *buffer;
	struct mail_raw *rawmail;
	struct edit_mail *edmail;
	struct mail *mail;
	const char *value;
	unsigned int i;

	test_begin("edit-mail - small buffer");
	test_edit_mail_init();

	/* compose the message */

	input_msg = i_stream_create_from_data(message, strlen(message));
	i_stream_set_max_buffer_size(input_msg, 16);

	rawmail = mail_raw_open_stream(test_raw_mail_user, input_msg);

	edmail = edit_mail_wrap(rawmail->mail);

	/* add headers */

	for (i = 0; i < 16; i++) {
		edit_mail_header_add(edmail, "X-F", "FF", FALSE);
		edit_mail_header_add(edmail, "X-L", "LL", TRUE);
	}
	
	mail = edit_mail_get_mail(edmail);

	/* prepare tests */

	if (mail_get_stream(mail, NULL, NULL, &input_mail) < 0) {
		i_fatal("Failed to open mail stream: %s",
			mailbox_get_last_internal_error(mail->box, NULL));
	}

	buffer = buffer_create_dynamic(default_pool, 1024);

	/* evaluate modified header */

	test_assert(mail_get_first_header_utf8(mail, "X-F", &value) > 0);
	test_assert(mail_get_first_header_utf8(mail, "X-A", &value) > 0);
	test_assert(mail_get_first_header_utf8(mail, "X-B", &value) > 0);
	test_assert(mail_get_first_header_utf8(mail, "X-L", &value) > 0);

	/* check stream read */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);

	test_stream_data(input_mail, buffer);

	/* clean up */

	buffer_free(&buffer);
	edit_mail_unwrap(&edmail);
	mail_raw_close(&rawmail);
	i_stream_unref(&input_msg);
	test_edit_mail_deinit();
	test_end();
}

static void test_edit_mail_empty(void)
{
	struct istream *input_msg, *input_mail;
	buffer_t *buffer;
	struct mail_raw *rawmail;
	struct edit_mail *edmail;
	struct mail *mail;
	const char *value;

	test_begin("edit-mail - empty message");
	test_edit_mail_init();

	/* Compose the message */

	input_msg = i_stream_create_from_data("", 0);

	rawmail = mail_raw_open_stream(test_raw_mail_user, input_msg);

	edmail = edit_mail_wrap(rawmail->mail);

	/* Add header */

	edit_mail_header_add(edmail, "X-B", "Frop", TRUE);
	mail = edit_mail_get_mail(edmail);

	/* Prepare tests */

	if (mail_get_stream(mail, NULL, NULL, &input_mail) < 0) {
		i_fatal("Failed to open mail stream: %s",
			mailbox_get_last_error(mail->box, NULL));
	}

	buffer = buffer_create_dynamic(default_pool, 1024);

	/* Evaluate modified header */

	test_assert(mail_get_first_header_utf8(mail, "X-B", &value) > 0 &&
		    strcmp(value, "Frop") == 0);

	/* Added */

	i_stream_seek(input_mail, 0);
	buffer_set_used_size(buffer, 0);

	test_stream_data(input_mail, buffer);

	/* Clean up */

	buffer_free(&buffer);
	edit_mail_unwrap(&edmail);
	mail_raw_close(&rawmail);
	i_stream_unref(&input_msg);
	test_edit_mail_deinit();
	test_end();
}

int main(int argc, char *argv[])
{
	static void (*test_functions[])(void) = {
		test_edit_mail_concatenated,
		test_edit_mail_big_header,
		test_edit_mail_small_buffer,
		test_edit_mail_empty,
		NULL
	};
	const enum master_service_flags service_flags =
		MASTER_SERVICE_FLAG_STANDALONE |
		MASTER_SERVICE_FLAG_DONT_SEND_STATS |
		MASTER_SERVICE_FLAG_CONFIG_BUILTIN;
	const char *error;
	int ret;

	master_service = master_service_init("test-edit-header", service_flags,
					     &argc, &argv, "");
	if (master_service_settings_read_simple(master_service, &error) < 0)
		i_fatal("%s", error);
	master_service_init_finish(master_service);

	test_init();
	test_dir_init("edit-mail");
	ret = test_run(test_functions);

	master_service_deinit(&master_service);

	return ret;
}

