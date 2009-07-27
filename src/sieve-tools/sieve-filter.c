/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "ostream.h"
#include "array.h"
#include "mail-namespace.h"
#include "mail-storage.h"
#include "mail-search-build.h"
#include "env-util.h"

#include "sieve.h"
#include "sieve-extensions.h"
#include "sieve-binary.h"

#include "mail-raw.h"
#include "sieve-tool.h"

#include "sieve-ext-debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

/*
 * Print help
 */

static void print_help(void)
{
	printf(
"Usage: sieve-filter [-m <mailbox>] [-x <extensions>] [-s <script-file>] [-c]\n"
"                    <script-file> <src-mail-store> [<dest-mail-store>]\n"
	);
}

enum discard_action_type {
	DISCARD_ACTION_KEEP,            /* Always keep messages in source folder */ 
	DISCARD_ACTION_DELETE,          /* Flag discarded messages as \DELETED */
	DISCARD_ACTION_TRASH_FOLDER,    /* Move discarded messages to Trash folder */      
	DISCARD_ACTION_EXPUNGE          /* Expunge discarded messages */
};

struct discard_action {
	enum discard_action_type type;
	const char *trash_folder;
};

static int filter_message
(struct mail *mail, struct sieve_binary *main_sbin, 
	struct sieve_script_env *senv, struct sieve_error_handler *ehandler,
	struct discard_action discard_action)
{
	struct sieve_exec_status estatus;
	struct sieve_binary *sbin;
	struct sieve_message_data msgdata;
	const char *recipient, *sender;
	int ret;

	sieve_tool_get_envelope_data(mail, &recipient, &sender);

	/* Initialize execution status */
	memset(&estatus, 0, sizeof(estatus));
	senv->exec_status = &estatus;

	/* Collect necessary message data */
	memset(&msgdata, 0, sizeof(msgdata));
	msgdata.mail = mail;
	msgdata.return_path = sender;
	msgdata.to_address = recipient;
	msgdata.auth_user = senv->username;
	(void)mail_get_first_header(mail, "Message-ID", &msgdata.id);

	/* Single script */
	sbin = main_sbin;
	main_sbin = NULL;

	/* Execute script */
	ret = sieve_execute(sbin, &msgdata, senv, ehandler, NULL);

	/* Handle message in source folder */
	if ( ret > 0 && !estatus.keep_original ) {
		switch ( discard_action.type ) {
		/* Leave it there */
		case DISCARD_ACTION_KEEP:
			sieve_info(ehandler, NULL, "message left in source folder");
			break;
		/* Flag message as \DELETED */
		case DISCARD_ACTION_DELETE:					
			sieve_info(ehandler, NULL, "message flagged as deleted in source folder");
			mail_update_flags(mail, MODIFY_ADD, MAIL_DELETED);
			break;
		/* Move message to Trash folder */
		case DISCARD_ACTION_TRASH_FOLDER:			
			sieve_info(ehandler, NULL, 
				"message in source folder moved to folder '%s'", 
				discard_action.trash_folder);
			break;
		/* Expunge the message immediately */
		case DISCARD_ACTION_EXPUNGE:
			sieve_info(ehandler, NULL, "message removed from source folder");
			mail_expunge(mail);
			break;
		/* Unknown */
		default:
			i_unreached();
			break;
		}
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
(struct mailbox *box, struct sieve_binary *main_sbin, 
	struct sieve_script_env *senv, struct sieve_error_handler *ehandler,
	struct discard_action discard_action)
{
	struct mail_search_args *search_args;
	struct mailbox_transaction_context *t;
	struct mail_search_context *search_ctx;
	struct mail *mail;
	int ret = 1;

	/* Sync mailbox */

	if ( mailbox_sync(box, MAILBOX_SYNC_FLAG_FULL_READ, 0, NULL) < 0 ) {
		return -1;
	}

	/* Search non-deleted messages in the source folder */

	search_args = mail_search_build_init();
	mail_search_build_add_flags(search_args, MAIL_DELETED, TRUE);

	/* Iterate through all requested messages */

	t = mailbox_transaction_begin(box, 0);
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
	
		ret = filter_message(mail, main_sbin, senv, ehandler, discard_action);
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

	if ( mailbox_sync(box, MAILBOX_SYNC_FLAG_FULL_WRITE, 0, NULL) < 0 ) {
		return -1;
	}
	
	return ret;
}

/*
 * Tool implementation
 */

int main(int argc, char **argv) 
{
	const char *scriptfile, *recipient, *sender, *extensions,
		*src_mailbox, *dst_mailbox, *src_mailstore, *dst_mailstore; 
	bool force_compile;
	struct mail_namespace *src_ns = NULL, *dst_ns = NULL;
	struct mail_user *mail_user = NULL;
	struct sieve_binary *main_sbin;
	struct sieve_script_env scriptenv;
	struct sieve_error_handler *ehandler;
	struct mail_storage *dst_storage, *src_storage;
	struct discard_action discard_action = 
		{ DISCARD_ACTION_KEEP, "Trash" };
	struct mailbox *src_box;
	enum mail_error error;
	enum mailbox_open_flags open_flags = 
		MAILBOX_OPEN_KEEP_RECENT | MAILBOX_OPEN_IGNORE_ACLS;
	const char *user, *home, *folder;
	int i;

	sieve_tool_init();
	
	/* Parse arguments */
	scriptfile = recipient = sender = extensions = src_mailstore = dst_mailstore 
		= NULL;
	src_mailbox = dst_mailbox = "INBOX";
	force_compile = FALSE;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-m") == 0) {
			/* default mailbox (keep box) */
			i++;
			if (i == argc) 
				i_fatal("Missing -m argument");
			dst_mailbox = argv[i];
		} else if (strcmp(argv[i], "-x") == 0) {
			/* extensions */
			i++;
			if (i == argc)
				i_fatal("Missing -x argument");
			extensions = argv[i];
		} else if (strcmp(argv[i], "-c") == 0) {
			/* force compile */
			force_compile = TRUE;
		} else if ( scriptfile == NULL ) {
			scriptfile = argv[i];
		} else if ( src_mailstore == NULL ) {
			src_mailstore = argv[i];
		} else if ( dst_mailstore == NULL ) {
			dst_mailstore = argv[i];
		} else {
			print_help();
			i_fatal("Unknown argument: %s", argv[i]);
		}
	}
	
	if ( scriptfile == NULL ) {
		print_help();
		i_fatal("Missing <scriptfile> argument");
	}
	
	if ( src_mailstore == NULL ) {
		print_help();
		i_fatal("Missing <mailstore> argument");
	}

	if ( extensions != NULL ) {
		sieve_set_extensions(extensions);
	}

	/* Register tool-specific extensions */
	(void) sieve_extension_register(&debug_extension, TRUE);

	/* Create error handler */
	ehandler = sieve_stderr_ehandler_create(0);
	sieve_system_ehandler_set(ehandler);
	sieve_error_handler_accept_infolog(ehandler, TRUE);

	/* Compile main sieve script */
	if ( force_compile ) {
		main_sbin = sieve_tool_script_compile(scriptfile, NULL);
		(void) sieve_save(main_sbin, NULL);
	} else {
		main_sbin = sieve_tool_script_open(scriptfile);
	}
	
	user = sieve_tool_get_user();
	home = getenv("HOME");

	/* Initialize mail storages */
	mail_users_init(getenv("AUTH_SOCKET_PATH"), getenv("DEBUG") != NULL);
	mail_storage_init();
	mail_storage_register_all();
	mailbox_list_register_all();

	/* Build namespaces environment (ugly!) */
	env_put(t_strdup_printf("NAMESPACE_1=%s", src_mailstore));
	env_put("NAMESPACE_1_INBOX=1");
	env_put("NAMESPACE_1_LIST=1");
	env_put("NAMESPACE_1_SEP=/");
	env_put("NAMESPACE_1_SUBSCRIPTIONS=1");

	if ( dst_mailstore != NULL ) {
		env_put(t_strdup_printf("NAMESPACE_2=%s", dst_mailstore));
		env_put("NAMESPACE_2_LIST=1");
		env_put("NAMESPACE_2_SEP=/");
		env_put("NAMESPACE_2_SUBSCRIPTIONS=1");

		env_put("NAMESPACE_1_PREFIX=#src/");
	}

	/* Initialize namespaces */
	mail_user = mail_user_init(user);
	mail_user_set_home(mail_user, home);
	if (mail_namespaces_init(mail_user) < 0)
		i_fatal("Namespace initialization failed");	

	if ( dst_mailstore != NULL ) {
		folder = "#src/";
		src_ns = mail_namespace_find(mail_user->namespaces, &folder);

		folder = "/";
		dst_ns = mail_namespace_find(mail_user->namespaces, &folder);	

		discard_action.type = DISCARD_ACTION_KEEP;	
	} else {
		dst_ns = src_ns = mail_user->namespaces;
		discard_action.type = DISCARD_ACTION_DELETE;	
	}

	src_storage = src_ns->storage;
	dst_storage = dst_ns->storage;

	/* Open the source mailbox */	
	src_box = mailbox_open(&src_storage, src_mailbox, NULL, open_flags);
	if ( src_box == NULL ) {
		i_fatal("Couldn't open mailbox '%s': %s", 
			src_mailbox, mail_storage_get_last_error(src_storage, &error));
	}

	/* Compose script environment */
	memset(&scriptenv, 0, sizeof(scriptenv));
	scriptenv.mailbox_autocreate = TRUE;
	scriptenv.default_mailbox = dst_mailbox;
	scriptenv.namespaces = dst_ns;
	scriptenv.username = user;
	scriptenv.hostname = "host.example.com";
	scriptenv.postmaster_address = "postmaster@example.com";
	scriptenv.smtp_open = NULL;
	scriptenv.smtp_close = NULL;
	scriptenv.duplicate_mark = NULL;
	scriptenv.duplicate_check = NULL;
	scriptenv.trace_stream = NULL;

	/* Apply Sieve filter to all messages found */
	(void) filter_mailbox
		(src_box, main_sbin, &scriptenv, ehandler, discard_action);
	
	/* Close the mailbox */
	if ( src_box != NULL )
		mailbox_close(&src_box);

	/* Cleanup error handler */
	sieve_error_handler_unref(&ehandler);
	sieve_system_ehandler_reset();	

	/* De-initialize mail user object */
	if ( mail_user != NULL )
		mail_user_unref(&mail_user);

	/* De-initialize mail storages */
	mail_storage_deinit();
	mail_users_deinit();	
	
	sieve_tool_deinit();
	
	return 0;
}
