/* Copyright (c) 2002-2010 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "lib-signals.h"
#include "ioloop.h"
#include "env-util.h"
#include "str.h"
#include "ostream.h"
#include "array.h"
#include "mail-namespace.h"
#include "mail-storage.h"
#include "mail-search-build.h"
#include "imap-utf7.h"

#include "sieve.h"
#include "sieve-extensions.h"
#include "sieve-binary.h"

#include "sieve-tool.h"

#include "sieve-ext-debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <sysexits.h>

/*
 * Print help
 */

static void print_help(void)
{
	printf(
"Usage: sieve-filter [-c <config-file>] [-C] [-D] [-e] [-m <default-mailbox>]\n"
"                    [-P <plugin>] [-q <output-mailbox>] [-Q <mail-command>]\n"
"                    [-s <script-file>] [-u <user>] [-W] [-x <extensions>]\n"
"                    <script-file> <source-mailbox> <source-action>\n"
	);
}

enum sieve_filter_source_action {
	SIEVE_FILTER_SACT_KEEP,          /* Always keep messages in source folder */ 
	SIEVE_FILTER_SACT_MOVE,          /* Move discarded messages to Trash folder */      
	SIEVE_FILTER_SACT_DELETE,        /* Flag discarded messages as \DELETED */
	SIEVE_FILTER_SACT_EXPUNGE        /* Expunge discarded messages */
};

struct sieve_filter_context {
	enum sieve_filter_source_action source_action;
	const char *move_mailbox;

	struct sieve_script_env *senv;
	struct sieve_binary *main_sbin;
	struct sieve_error_handler *ehandler;
};

static int filter_message
(struct sieve_filter_context *sfctx, struct mail *mail)
{
	struct sieve_error_handler *ehandler = sfctx->ehandler; 
	struct sieve_exec_status estatus;
	struct sieve_binary *sbin;
	struct sieve_message_data msgdata;
	const char *recipient, *sender;
	int ret;

	sieve_tool_get_envelope_data(mail, &recipient, &sender);

	/* Initialize execution status */
	memset(&estatus, 0, sizeof(estatus));
	sfctx->senv->exec_status = &estatus;

	/* Collect necessary message data */
	memset(&msgdata, 0, sizeof(msgdata));
	msgdata.mail = mail;
	msgdata.return_path = sender;
	msgdata.orig_envelope_to = recipient;
	msgdata.final_envelope_to = recipient;
	msgdata.auth_user = sfctx->senv->username;
	(void)mail_get_first_header(mail, "Message-ID", &msgdata.id);

	/* Single script */
	sbin = sfctx->main_sbin;

	/* Execute script */
	ret = sieve_execute(sbin, &msgdata, sfctx->senv, ehandler, NULL);

	/* Handle message in source folder */
	if ( ret > 0 && !estatus.keep_original ) {
		switch ( sfctx->source_action ) {
		/* Leave it there */
		case SIEVE_FILTER_SACT_KEEP:
			sieve_info(ehandler, NULL, "message left in source mailbox");
			break;
		/* Move message to Trash folder */
		case SIEVE_FILTER_SACT_MOVE:			
			sieve_info(ehandler, NULL, 
				"message in source mailbox moved to mailbox '%s'", sfctx->move_mailbox);
			break;
		/* Flag message as \DELETED */
		case SIEVE_FILTER_SACT_DELETE:					
			sieve_info(ehandler, NULL, "message flagged as deleted in source mailbox");
			mail_update_flags(mail, MODIFY_ADD, MAIL_DELETED);
			break;
		/* Expunge the message immediately */
		case SIEVE_FILTER_SACT_EXPUNGE:
			sieve_info(ehandler, NULL, "message expunged from source mailbox");
			mail_expunge(mail);
			break;
		/* Unknown */
		default:
			i_unreached();
			break;
		}
	} else {
		sieve_info(ehandler, NULL, "message left in source mailbox");
	}

	return ret;
}

/* FIXME: introduce this into Dovecot */
static void mail_search_build_add_flags
(struct mail_search_args *args, enum mail_flags flags, bool not)
{
	struct mail_search_arg *arg;

	arg = p_new(args->pool, struct mail_search_arg, 1);
	arg->type = SEARCH_FLAGS;
	arg->value.flags = flags;
	arg->not = not;

	arg->next = args->args;
	args->args = arg;
}

static int filter_mailbox
(struct sieve_filter_context *sfctx, struct mailbox *src_box)
{
	struct sieve_error_handler *ehandler = sfctx->ehandler;
	struct mail_search_args *search_args;
	struct mailbox_transaction_context *t;
	struct mail_search_context *search_ctx;
	struct mail *mail;
	int ret = 1;

	/* Sync mailbox */

	if ( mailbox_sync(src_box, MAILBOX_SYNC_FLAG_FULL_READ) < 0 ) {
		return -1;
	}

	/* Search non-deleted messages in the source folder */

	search_args = mail_search_build_init();
	mail_search_build_add_flags(search_args, MAIL_DELETED, TRUE);

	/* Iterate through all requested messages */

	t = mailbox_transaction_begin(src_box, 0);
	search_ctx = mailbox_search_init(t, search_args, NULL);
	mail_search_args_unref(&search_args);

	mail = mail_alloc(t, 0, NULL);
	while ( ret > 0 && mailbox_search_next(search_ctx, mail) > 0 ) {
		const char *subject, *date;
		uoff_t size = 0;
					
		/* Request message size */

		if ( mail_get_virtual_size(mail, &size) < 0 ) {
			if ( mail->expunged )
				continue;
			
			sieve_error(ehandler, NULL, "failed to obtain message size");
			continue;
		}

		if ( mail_get_first_header(mail, "date", &date) <= 0 )
			date = "";
		if ( mail_get_first_header(mail, "subject", &subject) <= 0 ) 
			subject = "";
		
		sieve_info(ehandler, NULL,
			"filtering: [%s; %"PRIuUOFF_T" bytes] %s", date, size, subject);
	
		ret = filter_message(sfctx, mail);
	}
	mail_free(&mail);
	
	/* Cleanup */

	if ( mailbox_search_deinit(&search_ctx) < 0 ) {
		ret = -1;
	}

	if ( ret < 0 ) {
		mailbox_transaction_rollback(&t);
		return -1;
	} else {
		if ( mailbox_transaction_commit(&t) < 0 ) {
			return -1;
		}
	}

	/* Sync mailbox */

	if ( mailbox_sync(src_box, MAILBOX_SYNC_FLAG_FULL_WRITE) < 0 ) {
		return -1;
	}
	
	return ret;
}

static const char *mailbox_name_to_mutf7(const char *mailbox_utf8)
{
	string_t *str = t_str_new(128);

	if (imap_utf8_to_utf7(mailbox_utf8, str) < 0)
		return mailbox_utf8;
	else
		return str_c(str);
}

/*
 * Tool implementation
 */

int main(int argc, char **argv) 
{
	struct sieve_instance *svinst;
	ARRAY_TYPE (const_string) scriptfiles;
	const char *scriptfile,	*src_mailbox, *dst_mailbox, *move_mailbox;
	struct sieve_filter_context sfctx;
	enum sieve_filter_source_action source_action = SIEVE_FILTER_SACT_KEEP;
	struct mail_user *mail_user;
	struct sieve_binary *main_sbin;
	struct sieve_script_env scriptenv;
	struct sieve_error_handler *ehandler;
	bool force_compile = FALSE, execute = FALSE, source_write = FALSE;
	struct mail_namespace *ns;
	struct mailbox *src_box;
	enum mailbox_flags open_flags =
		MAILBOX_FLAG_KEEP_RECENT | MAILBOX_FLAG_IGNORE_ACLS;
	enum mail_error error;
	int c;

	sieve_tool = sieve_tool_init("sieve-filter", &argc, &argv, 
		"R:m:s:x:P:CD", FALSE);

	t_array_init(&scriptfiles, 16);

	/* Parse arguments */
	scriptfile =  NULL;
	src_mailbox = dst_mailbox = "INBOX";
	move_mailbox = "Trash";
	force_compile = FALSE;
	while ((c = sieve_tool_getopt(sieve_tool)) > 0) {
		switch (c) {
		case 'm':
			/* default mailbox (keep box) */
			dst_mailbox = optarg;
			break;
		case 's': 
			/* scriptfile executed before main script */
			{
				const char *file;			

				file = t_strdup(optarg);
				array_append(&scriptfiles, &file, 1);

				/* FIXME: */
				i_fatal_status(EX_USAGE, 
					"The -s argument is currently NOT IMPLEMENTED");
			}
			break;
		case 'e':
			execute = TRUE;
			break;
		case 'C':
			force_compile = TRUE;
			break;
		case 'W':
			source_write = TRUE;
			break;
		default:
			print_help();
			i_fatal_status(EX_USAGE, "Unknown argument: %c", c);
			break;
		}
	}
	
	if ( optind < argc ) {
		scriptfile = t_strdup(argv[optind++]);
	} else {
		print_help();
		i_fatal_status(EX_USAGE, "Missing <script-file> argument");
	}

	if ( optind < argc ) {
		src_mailbox = t_strdup(argv[optind++]);
	} else {
		print_help();
		i_fatal_status(EX_USAGE, "Missing <source-mailbox> argument");
	}
		
	if ( optind < argc ) {
		const char *srcact = argv[optind++];

		if ( strcmp(srcact, "keep") == 0 ) {
			source_action = SIEVE_FILTER_SACT_KEEP;
		} else if ( strcmp(srcact, "move") == 0 ) {
			source_action = SIEVE_FILTER_SACT_MOVE;
			if ( optind < argc ) {
				move_mailbox = t_strdup(argv[optind++]);
			}
		} else if ( strcmp(srcact, "flag") == 0 ) {
			source_action = SIEVE_FILTER_SACT_DELETE;
		} else if ( strcmp(srcact, "expunge") == 0 ) {
			source_action = SIEVE_FILTER_SACT_EXPUNGE;
		} else {
			print_help();
			i_fatal_status(EX_USAGE, "Invalid <source-action> argument");
		}
	} 

	if ( optind != argc ) {
		print_help();
		i_fatal_status(EX_USAGE, "Unknown argument: %s", argv[optind]);
	}

	svinst = sieve_tool_init_finish(sieve_tool);

	/* Register Sieve debug extension */
	(void) sieve_extension_register(svinst, &debug_extension, TRUE);

	/* Create error handler */
	ehandler = sieve_stderr_ehandler_create(svinst, 0);
	sieve_system_ehandler_set(ehandler);
	sieve_error_handler_accept_infolog(ehandler, TRUE);

	/* Compile main sieve script */
	if ( force_compile ) {
		main_sbin = sieve_tool_script_compile(svinst, scriptfile, NULL);
		if ( main_sbin != NULL )
			(void) sieve_save(main_sbin, NULL, TRUE, NULL);
	} else {
		main_sbin = sieve_tool_script_open(svinst, scriptfile);
	}

	/* Initialize mail user */
	mail_user = sieve_tool_get_mail_user(sieve_tool);

	/* Find namespace for source mailbox */
	src_mailbox = mailbox_name_to_mutf7(src_mailbox);
	ns = mail_namespace_find(mail_user->namespaces, &src_mailbox);
	if ( ns == NULL )
		i_fatal("Unknown namespace for source mailbox '%s'", src_mailbox);

	/* Open the source mailbox */	
	if ( !source_write ) 
		open_flags |= MAILBOX_FLAG_READONLY;
	src_box = mailbox_alloc(ns->list, src_mailbox, open_flags);
  if ( mailbox_open(src_box) < 0 ) {
		i_fatal("Couldn't open mailbox '%s': %s", 
			src_mailbox, mail_storage_get_last_error(ns->storage, &error));
  }

	/* Compose script environment */
	memset(&scriptenv, 0, sizeof(scriptenv));
	scriptenv.mailbox_autocreate = TRUE;
	scriptenv.default_mailbox = dst_mailbox;
	scriptenv.user = mail_user;
	scriptenv.username = sieve_tool_get_username(sieve_tool);
	scriptenv.hostname = "host.example.com";
	scriptenv.postmaster_address = "postmaster@example.com";
	scriptenv.smtp_open = NULL;
	scriptenv.smtp_close = NULL;

	/* Compose filter context */
	memset(&sfctx, 0, sizeof(sfctx));
	sfctx.senv = &scriptenv;
	sfctx.source_action = source_action;
	sfctx.move_mailbox = move_mailbox;
	sfctx.main_sbin = main_sbin;
	sfctx.ehandler = ehandler;

	/* Apply Sieve filter to all messages found */
	(void) filter_mailbox(&sfctx, src_box);
	
	/* Close the mailbox */
	if ( src_box != NULL )
		mailbox_free(&src_box);

	/* Cleanup error handler */
	sieve_error_handler_unref(&ehandler);

	sieve_tool_deinit(&sieve_tool);

	return 0;
}
