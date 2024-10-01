/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "lib-signals.h"
#include "str.h"
#include "strfuncs.h"
#include "str-sanitize.h"
#include "unichar.h"
#include "array.h"
#include "eacces-error.h"
#include "smtp-params.h"
#include "istream.h"
#include "istream-crlf.h"
#include "istream-header-filter.h"
#include "ostream.h"
#include "mail-user.h"
#include "mail-storage.h"

#include "program-client.h"

#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-runtime.h"
#include "sieve-interpreter.h"

#include "sieve-ext-copy.h"
#include "sieve-ext-variables.h"

#include "sieve-extprograms-common.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

/*
 * Limits
 */

#define SIEVE_EXTPROGRAMS_MAX_PROGRAM_NAME_LEN 128
#define SIEVE_EXTPROGRAMS_MAX_PROGRAM_ARG_LEN  1024

#define SIEVE_EXTPROGRAMS_DEFAULT_EXEC_TIMEOUT_SECS 10
#define SIEVE_EXTPROGRAMS_CONNECT_TIMEOUT_MSECS 5

/*
 * Context
 */

int sieve_extprograms_ext_load(const struct sieve_extension *ext,
			       void **context_r)
{
	struct sieve_instance *svinst = ext->svinst;
	const struct sieve_extension *copy_ext = NULL;
	const struct sieve_extension *var_ext = NULL;
	struct sieve_extprograms_ext_context *extctx;
	const char *extname = sieve_extension_name(ext);
	const char *bin_dir, *socket_dir, *input_eol;
	sieve_number_t execute_timeout;

	extname = strrchr(extname, '.');
	i_assert(extname != NULL);
	extname++;

	bin_dir = sieve_setting_get(
		svinst, t_strdup_printf("sieve_%s_bin_dir", extname));
	socket_dir = sieve_setting_get(
		svinst, t_strdup_printf("sieve_%s_socket_dir", extname));
	input_eol = sieve_setting_get(
		svinst, t_strdup_printf("sieve_%s_input_eol", extname));

	if (sieve_extension_is(ext, sieve_ext_vnd_pipe)) {
		if (sieve_ext_copy_get_extension(ext->svinst, &copy_ext) < 0)
			return -1;
	}
	if (sieve_extension_is(ext, sieve_ext_vnd_execute)) {
		if (sieve_ext_variables_get_extension(ext->svinst,
						      &var_ext) < 0)
			return -1;
	}

	extctx = i_new(struct sieve_extprograms_ext_context, 1);
	extctx->execute_timeout =
		SIEVE_EXTPROGRAMS_DEFAULT_EXEC_TIMEOUT_SECS;
	extctx->copy_ext = copy_ext;
	extctx->var_ext = var_ext;

	if (bin_dir == NULL && socket_dir == NULL) {
		e_debug(svinst->event, "%s extension: "
			"no bin or socket directory specified; extension is unconfigured "
			"(both sieve_%s_bin_dir and sieve_%s_socket_dir are not set)",
			sieve_extension_name(ext), extname, extname);
	} else {
		extctx->bin_dir = i_strdup(bin_dir);
		extctx->socket_dir = i_strdup(socket_dir);

		if (sieve_setting_get_duration_value(
			svinst, t_strdup_printf("sieve_%s_exec_timeout",
						extname), &execute_timeout))
			extctx->execute_timeout = execute_timeout;

		extctx->default_input_eol = SIEVE_EXTPROGRAMS_EOL_CRLF;
		if (input_eol != NULL && strcasecmp(input_eol, "lf") == 0) {
			extctx->default_input_eol = SIEVE_EXTPROGRAMS_EOL_LF;
		}
	}

	*context_r = extctx;
	return 0;
}

void sieve_extprograms_ext_unload(const struct sieve_extension *ext)
{
	struct sieve_extprograms_ext_context *extctx = ext->context;

	if (extctx == NULL)
		return;

	i_free(extctx->bin_dir);
	i_free(extctx->socket_dir);
	i_free(extctx);
}

/*
 * Program name and arguments
 */

bool sieve_extprogram_name_is_valid(string_t *name)
{
	ARRAY_TYPE(unichars) uni_name;
	unsigned int count, i;
	const unichar_t *name_chars;
	size_t namelen = str_len(name);

	/* Check minimum length */
	if (namelen == 0)
		return FALSE;

	/* Check worst-case maximum length */
	if (namelen > SIEVE_EXTPROGRAMS_MAX_PROGRAM_NAME_LEN * 4)
		return FALSE;

	/* Intialize array for unicode characters */
	t_array_init(&uni_name, namelen * 4);

	/* Convert UTF-8 to UCS4/UTF-32 */
	if (uni_utf8_to_ucs4_n(str_data(name), namelen, &uni_name) < 0)
		return FALSE;
	name_chars = array_get(&uni_name, &count);

	/* Check true maximum length */
	if (count > SIEVE_EXTPROGRAMS_MAX_PROGRAM_NAME_LEN)
		return FALSE;

	/* Scan name for invalid characters
	     FIXME: compliance with Net-Unicode Definition (Section 2 of
	            RFC 5198) is not checked fully and no normalization
	            is performed.
	 */
	for (i = 0; i < count; i++) {
		/* 0000-001F; [CONTROL CHARACTERS] */
		if (name_chars[i] <= 0x001f)
			return FALSE;
		/* 002F; SLASH */
		if (name_chars[i] == 0x002f)
			return FALSE;
		/* 007F; DELETE */
		if (name_chars[i] == 0x007f)
			return FALSE;
		/* 0080-009F; [CONTROL CHARACTERS] */
		if (name_chars[i] >= 0x0080 && name_chars[i] <= 0x009f)
			return FALSE;
		/* 00FF */
		if (name_chars[i] == 0x00ff)
			return FALSE;
		/* 2028; LINE SEPARATOR */
		/* 2029; PARAGRAPH SEPARATOR */
		if (name_chars[i] == 0x2028 || name_chars[i] == 0x2029)
			return FALSE;
	}

	return TRUE;
}

bool sieve_extprogram_arg_is_valid(string_t *arg)
{
	const unsigned char *chars;
	unsigned int i;

	/* Check maximum length */
	if (str_len(arg) > SIEVE_EXTPROGRAMS_MAX_PROGRAM_ARG_LEN)
		return FALSE;

	/* Check invalid characters */
	chars = str_data(arg);
	for (i = 0; i < str_len(arg); i++) {
		/* 0010; CR */
		if (chars[i] == 0x0D)
			return FALSE;
		/* 0010; LF */
		if (chars[i] == 0x0A)
			return FALSE;
	}
	return TRUE;
}

/*
 * Command validation
 */

struct _arg_validate_context {
	struct sieve_validator *valdtr;
	struct sieve_command *cmd;
};

static int
_arg_validate(void *context, struct sieve_ast_argument *item)
{
	struct _arg_validate_context *actx =
		(struct _arg_validate_context *)context;

	if (sieve_argument_is_string_literal(item)) {
		string_t *arg = sieve_ast_argument_str(item);

		if (!sieve_extprogram_arg_is_valid(arg)) {
			sieve_argument_validate_error(
				actx->valdtr, item,
				"%s %s: specified external program argument '%s' is invalid",
				sieve_command_identifier(actx->cmd),
				sieve_command_type_name(actx->cmd),
				str_sanitize(str_c(arg), 128));
			return -1;
		}
	}
	return 1;
}

bool sieve_extprogram_command_validate(struct sieve_validator *valdtr,
				       struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct sieve_ast_argument *stritem;
	struct _arg_validate_context actx;
	string_t *program_name;

	if (arg == NULL) {
		sieve_command_validate_error(
			valdtr, cmd,
			"the %s %s expects at least one positional argument, "
			"but none was found",
			sieve_command_identifier(cmd),
			sieve_command_type_name(cmd));
		return FALSE;
	}

	/* <program-name: string> argument */

	if (!sieve_validate_positional_argument(
		valdtr, cmd, arg, "program-name", 1, SAAT_STRING))
		return FALSE;
	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;

	/* Variables are not allowed */
	if (!sieve_argument_is_string_literal(arg)) {
		sieve_argument_validate_error(valdtr, arg,
			"the %s %s requires a constant string "
			"for its program-name argument",
			sieve_command_identifier(cmd),
			sieve_command_type_name(cmd));
		return FALSE;
	}

	/* Check program name */
	program_name = sieve_ast_argument_str(arg);
	if (!sieve_extprogram_name_is_valid(program_name)) {
 		sieve_argument_validate_error(valdtr, arg,
			"%s %s: invalid program name '%s'",
			sieve_command_identifier(cmd),
			sieve_command_type_name(cmd),
			str_sanitize(str_c(program_name), 80));
		return FALSE;
	}

	/* Optional <arguments: string-list> argument */

	arg = sieve_ast_argument_next(arg);
	if (arg == NULL)
		return TRUE;

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "arguments",
						2, SAAT_STRING_LIST))
		return FALSE;
	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;

	/* Check arguments */
	actx.valdtr = valdtr;
	actx.cmd = cmd;
	stritem = arg;
	if (sieve_ast_stringlist_map(&stritem, &actx,
				     _arg_validate) <= 0)
		return FALSE;

	if (sieve_ast_argument_next(arg) != NULL) {
		sieve_command_validate_error(valdtr, cmd,
			"the %s %s expects at most two positional arguments, "
			"but more were found",
			sieve_command_identifier(cmd),
			sieve_command_type_name(cmd));
		return FALSE;
	}
	return TRUE;
}

/*
 * Common command operands
 */

int sieve_extprogram_command_read_operands(
	const struct sieve_runtime_env *renv, sieve_size_t *address,
	string_t **pname_r, struct sieve_stringlist **args_list_r)
{
	string_t *arg;
	int ret;

	/*
	 * Read fixed operands
	 */

	ret = sieve_opr_string_read(renv, address, "program-name", pname_r);
	if (ret <= 0)
		return ret;
	ret = sieve_opr_stringlist_read_ex(renv, address, "arguments", TRUE,
					   args_list_r);
	if (ret <= 0)
		return ret;

	/*
	 * Check operands
	 */

	arg = NULL;
	while (*args_list_r != NULL &&
	       (ret = sieve_stringlist_next_item(*args_list_r, &arg)) > 0) {
		if (!sieve_extprogram_arg_is_valid(arg)) {
			sieve_runtime_error(renv, NULL,
				"specified :args item '%s' is invalid",
				str_sanitize(str_c(arg), 128));
			return SIEVE_EXEC_FAILURE;
		}
	}
	if (ret < 0) {
		sieve_runtime_trace_error(renv, "invalid args-list item");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	return SIEVE_EXEC_OK;
}

/*
 * Running external programs
 */

struct sieve_extprogram {
	struct sieve_instance *svinst;
	const struct sieve_extprograms_ext_context *extctx;

	const struct sieve_script_env *scriptenv;
	struct program_client *program_client;
};

void sieve_extprogram_exec_error(struct sieve_error_handler *ehandler,
				 const char *location, const char *fmt, ...)
{
	char str[256];
	struct tm *tm;
	const char *timestamp;

	tm = localtime(&ioloop_time);

	timestamp =
		(strftime(str, sizeof(str), " [%Y-%m-%d %H:%M:%S]", tm) > 0 ?
		 str : "");

	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_error(ehandler, location,
			    "%s: refer to server log for more information.%s",
			    t_strdup_vprintf(fmt, args), timestamp);
	} T_END;

	va_end(args);
}

/* API */

struct sieve_extprogram *
sieve_extprogram_create(const struct sieve_extension *ext,
			const struct sieve_script_env *senv,
			const struct sieve_message_data *msgdata,
			const char *action, const char *program_name,
			const char *const *args,
			enum sieve_error *error_code_r)
{
	struct sieve_instance *svinst = ext->svinst;
	struct sieve_extprograms_ext_context *extctx = ext->context;
	const struct smtp_address *sender, *recipient, *orig_recipient;
	struct sieve_extprogram *sprog;
	const char *path = NULL;
	struct stat st;
	bool fork = FALSE;

	e_debug(svinst->event, "action %s: "
		"running program: %s", action, program_name);

	if (extctx == NULL ||
	    (extctx->bin_dir == NULL && extctx->socket_dir == NULL)) {
		e_error(svinst->event, "action %s: "
			"failed to execute program '%s': "
			"vnd.dovecot.%s extension is unconfigured",
			action, program_name, action);
		*error_code_r = SIEVE_ERROR_NOT_FOUND;
		return NULL;
	}

	/* Try socket first */
	if (extctx->socket_dir != NULL) {
		path = t_strconcat(senv->user->set->base_dir, "/",
				   extctx->socket_dir, "/", program_name,
				   NULL);
		if (stat(path, &st) < 0) {
			switch (errno) {
			case ENOENT:
				e_debug(svinst->event, "action %s: "
					"socket path '%s' for program '%s' not found",
					action, path, program_name);
				break;
			case EACCES:
				e_error(svinst->event, "action %s: "
					"failed to stat socket: %s",
					action, eacces_error_get("stat", path));
				*error_code_r = SIEVE_ERROR_NO_PERMISSION;
				return NULL;
			default:
				e_error(svinst->event, "action %s: "
					"failed to stat socket '%s': %m",
					action, path);
				*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
				return NULL;
			}
			path = NULL;
		} else if (!S_ISSOCK(st.st_mode)) {
			e_error(svinst->event, "action %s: "
				"socket path '%s' for program '%s' is not a socket",
				action, path, program_name);
			*error_code_r = SIEVE_ERROR_NOT_POSSIBLE;
			return NULL;
		}
	}

	/* Try executable next */
	if (path == NULL && extctx->bin_dir != NULL) {
		fork = TRUE;
		path = t_strconcat(extctx->bin_dir, "/", program_name, NULL);
		if (stat(path, &st) < 0) {
			switch (errno) {
			case ENOENT:
				e_debug(svinst->event, "action %s: "
					"executable path '%s' for program '%s' not found",
					action, path, program_name);
				*error_code_r = SIEVE_ERROR_NOT_FOUND;
				break;
			case EACCES:
				e_error(svinst->event, "action %s: "
					"failed to stat program: %s",
					action, eacces_error_get("stat", path));
				*error_code_r = SIEVE_ERROR_NO_PERMISSION;
				break;
			default:
				e_error(svinst->event, "action %s: "
					"failed to stat program '%s': %m",
					action, path);
				*error_code_r = SIEVE_ERROR_TEMP_FAILURE;
				break;
			}

			return NULL;
		} else if (!S_ISREG(st.st_mode)) {
			e_error(svinst->event, "action %s: "
				"executable '%s' for program '%s' is not a regular file",
				action, path, program_name);
			*error_code_r = SIEVE_ERROR_NOT_POSSIBLE;
			return NULL;
		} else if ((st.st_mode & S_IWOTH) != 0) {
			e_error(svinst->event, "action %s: "
				"executable '%s' for program '%s' is world-writable",
				action, path, program_name);
			*error_code_r = SIEVE_ERROR_NO_PERMISSION;
			return NULL;
		}
	}

	/* None found ? */
	if (path == NULL) {
		e_error(svinst->event, "action %s: "
			"program '%s' not found", action, program_name);
		*error_code_r = SIEVE_ERROR_NOT_FOUND;
		return NULL;
	}

	sprog = i_new(struct sieve_extprogram, 1);
	sprog->svinst = ext->svinst;
	sprog->extctx = extctx;
	sprog->scriptenv = senv;

	struct program_client_parameters pc_params = {
		.client_connect_timeout_msecs =
			SIEVE_EXTPROGRAMS_CONNECT_TIMEOUT_MSECS,
		.input_idle_timeout_msecs = extctx->execute_timeout * 1000,
	};

	if (fork) {
		sprog->program_client =	program_client_local_create(
			svinst->event, path, args, &pc_params);
	} else {
		sprog->program_client = program_client_unix_create(
			svinst->event, path, args, &pc_params);
	}

	program_client_set_env(sprog->program_client, "USER",
			       svinst->username);
	if (svinst->home_dir != NULL) {
		program_client_set_env(sprog->program_client, "HOME",
				       svinst->home_dir);
	}
	if (svinst->hostname != NULL) {
		program_client_set_env(sprog->program_client, "HOST",
				       svinst->hostname);
	}

	sender = msgdata->envelope.mail_from;
	recipient = msgdata->envelope.rcpt_to;
	orig_recipient = NULL;
	if (msgdata->envelope.rcpt_params != NULL)
		orig_recipient = msgdata->envelope.rcpt_params->orcpt.addr;

	if (!smtp_address_isnull(sender)) {
		program_client_set_env(sprog->program_client, "SENDER",
				       smtp_address_encode(sender));
	}
	if (!smtp_address_isnull(recipient)) {
		program_client_set_env(sprog->program_client, "RECIPIENT",
				       smtp_address_encode(recipient));
	}
	if (!smtp_address_isnull(orig_recipient)) {
		program_client_set_env(sprog->program_client, "ORIG_RECIPIENT",
				       smtp_address_encode(orig_recipient));
	}

	return sprog;
}

void sieve_extprogram_destroy(struct sieve_extprogram **_sprog)
{
	struct sieve_extprogram *sprog = *_sprog;

	program_client_destroy(&sprog->program_client);
	i_free(sprog);
	*_sprog = NULL;
}

/* I/0 */

void sieve_extprogram_set_output(struct sieve_extprogram *sprog,
				 struct ostream *output)
{
	program_client_set_output(sprog->program_client, output);
}

void sieve_extprogram_set_input(struct sieve_extprogram *sprog,
				struct istream *input)
{
	switch (sprog->extctx->default_input_eol) {
	case SIEVE_EXTPROGRAMS_EOL_LF:
		input = i_stream_create_lf(input);
		break;
	case SIEVE_EXTPROGRAMS_EOL_CRLF:
		input = i_stream_create_crlf(input);
		break;
	default:
		i_unreached();
	}

	program_client_set_input(sprog->program_client, input);

	i_stream_unref(&input);
}

void sieve_extprogram_set_output_seekable(struct sieve_extprogram *sprog)
{
	string_t *prefix;
	prefix = t_str_new(128);
	mail_user_set_get_temp_prefix(prefix, sprog->scriptenv->user->set);

	program_client_set_output_seekable(sprog->program_client,
					   str_c(prefix));
}

struct istream *
sieve_extprogram_get_output_seekable(struct sieve_extprogram *sprog)
{
	return program_client_get_output_seekable(sprog->program_client);
}

int sieve_extprogram_set_input_mail(struct sieve_extprogram *sprog,
				    struct mail *mail)
{
	struct istream *input;

	if (mail_get_stream(mail, NULL, NULL, &input) < 0)
		return -1;

	sieve_extprogram_set_input(sprog, input);
	return 1;
}

int sieve_extprogram_run(struct sieve_extprogram *sprog)
{
	switch (program_client_run(sprog->program_client)) {
	case PROGRAM_CLIENT_EXIT_STATUS_INTERNAL_FAILURE:
		return -1;
	case PROGRAM_CLIENT_EXIT_STATUS_FAILURE:
		return 0;
	case PROGRAM_CLIENT_EXIT_STATUS_SUCCESS:
		return 1;
	}
	i_unreached();
}
