/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "lib-signals.h"
#include "array.h"
#include "ioloop.h"
#include "ostream.h"
#include "hostpid.h"
#include "dict.h"
#include "mail-namespace.h"
#include "mail-storage.h"
#include "mail-user.h"
#include "message-address.h"
#include "smtp-params.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "mail-storage-service.h"

#include "sieve.h"
#include "sieve-plugins.h"
#include "sieve-extensions.h"

#include "mail-raw.h"

#include "sieve-tool.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <sysexits.h>

/*
 * Global state
 */

struct sieve_tool {
	char *name;

	bool no_config;

	char *username;
	char *homedir;

	char *sieve_extensions;
	ARRAY_TYPE(const_string) sieve_plugins;

	sieve_tool_setting_callback_t setting_callback;
	void *setting_callback_context;

	struct sieve_instance *svinst;

	struct mail_storage_service_ctx *storage_service;
	struct mail_user *mail_user_dovecot;
	struct mail_user *mail_user;

	struct mail_user *mail_raw_user;
	struct mail_raw *mail_raw;

	bool debug:1;
};

struct sieve_tool *sieve_tool;

/*
 * Settings management
 */

static const char *
sieve_tool_sieve_get_setting(void *context, const char *identifier)
{
	struct sieve_tool *tool = (struct sieve_tool *)context;

	if (tool->setting_callback != NULL) {
		return tool->setting_callback(tool->setting_callback_context,
					      identifier);
	}

	if (tool->mail_user_dovecot == NULL)
		return NULL;

	return mail_user_plugin_getenv(tool->mail_user_dovecot, identifier);
}

static const char *sieve_tool_sieve_get_homedir(void *context)
{
	struct sieve_tool *tool = (struct sieve_tool *)context;

	return sieve_tool_get_homedir(tool);
}

const struct sieve_callbacks sieve_tool_callbacks = {
	sieve_tool_sieve_get_homedir,
	sieve_tool_sieve_get_setting,
};

/*
 * Initialization
 */

static void
sieve_tool_get_user_data(const char **username_r, const char **homedir_r)
{
	uid_t process_euid = geteuid();
	struct passwd *pw;
	const char *user = NULL, *home = NULL;

	user = getenv("USER");
	home = getenv("HOME");

	if (user == NULL || *user == '\0' || home == NULL || *home == '\0') {
		pw = getpwuid(process_euid);
		if (pw != NULL) {
			user = pw->pw_name;
			home = pw->pw_dir;
		}
	}

	if (username_r != NULL) {
		if (user == NULL || *user == '\0') {
			i_fatal("couldn't lookup our username (uid=%s)",
				dec2str(process_euid));
		}

		*username_r = t_strdup(user);
	}

	if (homedir_r != NULL)
		*homedir_r = t_strdup(home);
}

struct sieve_tool *
sieve_tool_init(const char *name, int *argc, char **argv[],
		const char *getopt_str, bool no_config)
{
	struct sieve_tool *tool;
	enum master_service_flags service_flags =
		MASTER_SERVICE_FLAG_STANDALONE |
		MASTER_SERVICE_FLAG_DONT_SEND_STATS |
		MASTER_SERVICE_FLAG_NO_INIT_DATASTACK_FRAME;

	if (no_config)
		service_flags |= MASTER_SERVICE_FLAG_NO_CONFIG_SETTINGS;

	master_service = master_service_init(name, service_flags,
					     argc, argv, getopt_str);

	tool = i_new(struct sieve_tool, 1);
	tool->name = i_strdup(name);
	tool->no_config = no_config;

	i_array_init(&tool->sieve_plugins, 16);
	return tool;
}

int sieve_tool_getopt(struct sieve_tool *tool)
{
	int c;

	while ((c = master_getopt(master_service)) > 0) {
		switch (c) {
		case 'x':
			/* Extensions */
			if (tool->sieve_extensions != NULL) {
				i_fatal_status(
					EX_USAGE,
					"duplicate -x option specified, "
					"but only one allowed.");
			}
			tool->sieve_extensions = i_strdup(optarg);
			break;
		case 'u':
			if (tool->username == NULL)
				tool->username = i_strdup(optarg);
			break;
		case 'P': {
			/* Plugin */
			const char *plugin;

			plugin = t_strdup(optarg);
			array_append(&tool->sieve_plugins, &plugin, 1);
			break;
		}
		case 'D':
			tool->debug = TRUE;
			break;
		default:
			return c;
		}
	}

	return c;
}

static void sieve_tool_load_plugins(struct sieve_tool *tool)
{
	unsigned int i, count;
	const char *const *plugins;

	plugins = array_get(&tool->sieve_plugins, &count);
	for (i = 0; i < count; i++) {
		const char *path, *file = strrchr(plugins[i], '/');

		if (file != NULL) {
			path = t_strdup_until(plugins[i], file);
			file = file+1;
		} else {
			path = NULL;
			file = plugins[i];
		}

		sieve_plugins_load(tool->svinst, path, file);
	}
}

struct sieve_instance *
sieve_tool_init_finish(struct sieve_tool *tool, bool init_mailstore,
		       bool preserve_root)
{
	enum mail_storage_service_flags storage_service_flags =
		MAIL_STORAGE_SERVICE_FLAG_NO_CHDIR |
		MAIL_STORAGE_SERVICE_FLAG_NO_LOG_INIT;
	struct mail_storage_service_input service_input;
	struct sieve_environment svenv;
	const char *username = tool->username;
	const char *homedir = tool->homedir;
	const char *errstr;

	if (master_service_settings_read_simple(master_service, &errstr) < 0)
		i_fatal("%s", errstr);

	master_service_init_finish(master_service);

	if (username == NULL) {
		sieve_tool_get_user_data(&username, &homedir);

		username = tool->username = i_strdup(username);

		if (tool->homedir != NULL)
			i_free(tool->homedir);
		tool->homedir = i_strdup(homedir);

		if (preserve_root) {
			storage_service_flags |=
				MAIL_STORAGE_SERVICE_FLAG_NO_RESTRICT_ACCESS;
		}
	} else {
		storage_service_flags |=
			MAIL_STORAGE_SERVICE_FLAG_USERDB_LOOKUP;
	}

	if (!init_mailstore)
		storage_service_flags |=
			MAIL_STORAGE_SERVICE_FLAG_NO_NAMESPACES;

	i_zero(&service_input);
	service_input.service = tool->name;
	service_input.username = username;

	tool->storage_service = mail_storage_service_init(
		master_service, storage_service_flags);
	if (mail_storage_service_lookup_next(
		tool->storage_service, &service_input,
		&tool->mail_user_dovecot, &errstr) <= 0)
		i_fatal("%s", errstr);

	i_zero(&svenv);
	svenv.username = username;
	(void)mail_user_get_home(tool->mail_user_dovecot, &svenv.home_dir);
	svenv.hostname = my_hostdomain();
	svenv.base_dir = tool->mail_user_dovecot->set->base_dir;
	svenv.temp_dir = tool->mail_user_dovecot->set->mail_temp_dir;
	svenv.location = SIEVE_ENV_LOCATION_MS;
	svenv.delivery_phase = SIEVE_DELIVERY_PHASE_POST;

	/* Initialize Sieve Engine */
	tool->svinst = sieve_init(&svenv, &sieve_tool_callbacks, tool,
				  tool->debug);
	if (tool->svinst == NULL)
		i_fatal("failed to initialize sieve implementation");

	/* Load Sieve plugins */
	if (array_count(&tool->sieve_plugins) > 0)
		sieve_tool_load_plugins(tool);

	/* Set active Sieve extensions */
	if (tool->sieve_extensions != NULL) {
		sieve_set_extensions(tool->svinst, tool->sieve_extensions);
	} else if (tool->no_config) {
		sieve_set_extensions(tool->svinst, NULL);
	}

	return tool->svinst;
}

void sieve_tool_deinit(struct sieve_tool **_tool)
{
	struct sieve_tool *tool = *_tool;

	*_tool = NULL;

	/* Deinitialize Sieve engine */
	sieve_deinit(&tool->svinst);

	/* Free options */

	if (tool->username != NULL)
		i_free(tool->username);
	if (tool->homedir != NULL)
		i_free(tool->homedir);

	if (tool->sieve_extensions != NULL)
		i_free(tool->sieve_extensions);
	array_free(&tool->sieve_plugins);

	/* Free raw mail */

	if (tool->mail_raw != NULL)
		mail_raw_close(&tool->mail_raw);

	if (tool->mail_raw_user != NULL)
		mail_user_unref(&tool->mail_raw_user);

	/* Free mail service */

	if (tool->mail_user != NULL)
		mail_user_unref(&tool->mail_user);
	if (tool->mail_user_dovecot != NULL)
		mail_user_unref(&tool->mail_user_dovecot);

	mail_storage_service_deinit(&tool->storage_service);

	/* Free sieve tool object */

	i_free(tool->name);
	i_free(tool);

	/* Deinitialize service */
	master_service_deinit(&master_service);
}

/*
 * Mail environment
 */

void sieve_tool_init_mail_user(struct sieve_tool *tool)
{
	struct mail_user *mail_user_dovecot = tool->mail_user_dovecot;
	const char *username = tool->username;
	struct mail_namespace *ns = NULL;
	const char *home = NULL, *errstr = NULL;

	struct settings_instance *set_instance =
		mail_storage_service_user_get_settings_instance(
			mail_user_dovecot->service_user);
	struct mail_storage_service_input input = {
		.username = username,
		.set_instance = set_instance,
		.no_userdb_lookup = TRUE,
	};
	if (mail_storage_service_lookup_next(tool->storage_service, &input,
					     &tool->mail_user, &errstr) < 0)
		i_fatal("Test user lookup failed: %s", errstr);

	home = sieve_tool_get_homedir(sieve_tool);
	if (home != NULL)
		mail_user_set_home(tool->mail_user, home);

	if (mail_user_init(tool->mail_user, &errstr) < 0)
 		i_fatal("Test user initialization failed: %s", errstr);

	if (mail_namespaces_init_location(tool->mail_user,
					  tool->mail_user->event, &errstr) < 0)
		i_fatal("Test storage creation failed: %s", errstr);

	ns = tool->mail_user->namespaces;
	ns->flags |= NAMESPACE_FLAG_NOQUOTA | NAMESPACE_FLAG_NOACL;
}

static void sieve_tool_init_mail_raw_user(struct sieve_tool *tool)
{
	if (tool->mail_raw_user == NULL) {
		tool->mail_raw_user = mail_raw_user_create(
			tool->mail_user_dovecot);
	}
}

struct mail *
sieve_tool_open_file_as_mail(struct sieve_tool *tool, const char *path)
{
	sieve_tool_init_mail_raw_user(tool);

	if (tool->mail_raw != NULL)
		mail_raw_close(&tool->mail_raw);

	tool->mail_raw = mail_raw_open_file(tool->mail_raw_user, path);

	return tool->mail_raw->mail;
}

struct mail *
sieve_tool_open_data_as_mail(struct sieve_tool *tool, string_t *mail_data)
{
	sieve_tool_init_mail_raw_user(tool);

	if (tool->mail_raw != NULL)
		mail_raw_close(&tool->mail_raw);

	tool->mail_raw = mail_raw_open_data(tool->mail_raw_user, mail_data);

	return tool->mail_raw->mail;
}

/*
 * Configuration
 */

void sieve_tool_set_homedir(struct sieve_tool *tool, const char *homedir)
{
	if (tool->homedir != NULL) {
		if (strcmp(homedir, tool->homedir) == 0)
			return;

		i_free(tool->homedir);
	}

	tool->homedir = i_strdup(homedir);

	if (tool->mail_user_dovecot != NULL)
		mail_user_set_home(tool->mail_user_dovecot, tool->homedir);
	if (tool->mail_user != NULL)
		mail_user_set_home(tool->mail_user, tool->homedir);
}

void sieve_tool_set_setting_callback(struct sieve_tool *tool,
				     sieve_tool_setting_callback_t callback,
				     void *context)
{
	tool->setting_callback = callback;
	tool->setting_callback_context = context;
}

/*
 * Accessors
 */

const char *sieve_tool_get_username(struct sieve_tool *tool)
{
	const char *username;

	if (tool->username == NULL) {
		sieve_tool_get_user_data(&username, NULL);
		return username;
	}

	return tool->username;
}

const char *sieve_tool_get_homedir(struct sieve_tool *tool)
{
	const char *homedir = NULL;

	if (tool->homedir != NULL)
		return tool->homedir;

	if (tool->mail_user_dovecot != NULL &&
	    mail_user_get_home(tool->mail_user_dovecot, &homedir) > 0)
		return tool->homedir;

	sieve_tool_get_user_data(NULL, &homedir);
	return homedir;
}

struct mail_user *sieve_tool_get_mail_user(struct sieve_tool *tool)
{
	return (tool->mail_user == NULL ?
		tool->mail_user_dovecot : tool->mail_user);
}

struct mail_user *sieve_tool_get_mail_raw_user(struct sieve_tool *tool)
{
	sieve_tool_init_mail_raw_user(tool);
	return tool->mail_raw_user;
}

struct mail_storage_service_ctx *
sieve_tool_get_mail_storage_service(struct sieve_tool *tool)
{
	sieve_tool_init_mail_raw_user(tool);
	return tool->storage_service;
}

/*
 * Commonly needed functionality
 */

static const struct smtp_address *
sieve_tool_get_address(struct mail *mail, const char *header)
{
	struct message_address *addr;
	struct smtp_address *smtp_addr;
	const char *str;

	if (mail_get_first_header(mail, header, &str) <= 0)
		return NULL;
	addr = message_address_parse(pool_datastack_create(),
				     (const unsigned char *)str,
				     strlen(str), 1, 0);
	if (addr == NULL || addr->mailbox == NULL ||
	    addr->domain == NULL || *addr->mailbox == '\0' ||
	    *addr->domain == '\0')
		return NULL;
	if (smtp_address_create_from_msg_temp(addr, &smtp_addr) < 0)
		return NULL;
	return smtp_addr;
}

void sieve_tool_get_envelope_data(struct sieve_message_data *msgdata,
				  struct mail *mail,
				  const struct smtp_address *sender,
				  const struct smtp_address *rcpt_orig,
				  const struct smtp_address *rcpt_final)
{
	struct smtp_params_rcpt *rcpt_params;

	/* Get sender address */
	if (sender == NULL)
		sender = sieve_tool_get_address(mail, "Return-path");
	if (sender == NULL)
		sender = sieve_tool_get_address(mail, "Sender");
	if (sender == NULL)
		sender = sieve_tool_get_address(mail, "From");
	if (sender == NULL)
		sender = smtp_address_create_temp("sender", "example.com");

	/* Get recipient address */
	if (rcpt_final == NULL)
		rcpt_final = sieve_tool_get_address(mail, "Envelope-To");
	if (rcpt_final == NULL)
		rcpt_final = sieve_tool_get_address(mail, "To");
	if (rcpt_final == NULL) {
		rcpt_final = smtp_address_create_temp("recipient",
						      "example.com");
	}
	if (rcpt_orig == NULL)
		rcpt_orig = rcpt_final;

	msgdata->envelope.mail_from = sender;
	msgdata->envelope.rcpt_to = rcpt_final;

	rcpt_params = t_new(struct smtp_params_rcpt, 1);
	rcpt_params->orcpt.addr = rcpt_orig;

	msgdata->envelope.rcpt_params = rcpt_params;
}

/*
 * File I/O
 */

struct ostream *sieve_tool_open_output_stream(const char *filename)
{
	struct ostream *outstream;
	int fd;

	if (strcmp(filename, "-") == 0)
		outstream = o_stream_create_fd(1, 0);
	else {
		fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, 0600);
		if (fd < 0)
			i_fatal("failed to open file for writing: %m");

		outstream = o_stream_create_fd_autoclose(&fd, 0);
	}

	return outstream;
}

/*
 * Sieve script handling
 */

struct sieve_binary *
sieve_tool_script_compile(struct sieve_instance *svinst,
			  const char *filename, const char *name)
{
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;

	ehandler = sieve_stderr_ehandler_create(svinst, 0);
	sieve_error_handler_accept_infolog(ehandler, TRUE);
	sieve_error_handler_accept_debuglog(ehandler, svinst->debug);

	sbin = sieve_compile(svinst, filename, name, ehandler, 0, NULL);
	if (sbin == NULL)
		i_fatal("failed to compile sieve script '%s'", filename);

	sieve_error_handler_unref(&ehandler);
	return sbin;
}

struct sieve_binary *
sieve_tool_script_open(struct sieve_instance *svinst, const char *filename)
{
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;

	ehandler = sieve_stderr_ehandler_create(svinst, 0);
	sieve_error_handler_accept_infolog(ehandler, TRUE);
	sieve_error_handler_accept_debuglog(ehandler, svinst->debug);

	sbin = sieve_open(svinst, filename, NULL, ehandler, 0, NULL);
	if (sbin == NULL) {
		sieve_error_handler_unref(&ehandler);
		i_fatal("failed to compile sieve script");
	}

	sieve_error_handler_unref(&ehandler);

	sieve_save(sbin, FALSE, NULL);
	return sbin;
}

void sieve_tool_dump_binary_to(struct sieve_binary *sbin,
			       const char *filename, bool hexdump)
{
	struct ostream *dumpstream;

	if (filename == NULL)
		return;

	dumpstream = sieve_tool_open_output_stream(filename);
	if (dumpstream != NULL) {
		if (hexdump)
			(void)sieve_hexdump(sbin, dumpstream);
		else
			(void)sieve_dump(sbin, dumpstream, FALSE);
		if (o_stream_finish(dumpstream) < 0) {
			i_fatal("write(%s) failed: %s", filename,
				o_stream_get_error(dumpstream));
		}
		o_stream_destroy(&dumpstream);
	} else {
		i_fatal("Failed to create stream for sieve code dump.");
	}
}

/*
 * Commandline option parsing
 */

void sieve_tool_parse_trace_option(struct sieve_trace_config *tr_config,
				   const char *tr_option)
{
	const char *lvl;

	if (str_begins(tr_option, "level=", &lvl)) {
		if (strcmp(lvl, "none") == 0) {
			tr_config->level = SIEVE_TRLVL_NONE;
		} else if (strcmp(lvl, "actions") == 0) {
			tr_config->level = SIEVE_TRLVL_ACTIONS;
		} else if (strcmp(lvl, "commands") == 0) {
			tr_config->level = SIEVE_TRLVL_COMMANDS;
		} else if (strcmp(lvl, "tests") == 0) {
			tr_config->level = SIEVE_TRLVL_TESTS;
		} else if (strcmp(lvl, "matching") == 0) {
			tr_config->level = SIEVE_TRLVL_MATCHING;
		} else {
			i_fatal_status(EX_USAGE,
				       "Unknown -tlevel= trace level: %s", lvl);
		}
	} else if (strcmp(tr_option, "debug") == 0) {
		tr_config->flags |= SIEVE_TRFLG_DEBUG;
	} else if (strcmp(tr_option, "addresses") == 0) {
		tr_config->flags |= SIEVE_TRFLG_ADDRESSES;
	} else {
		i_fatal_status(EX_USAGE, "Unknown -t trace option value: %s",
			       tr_option);
	}
}
