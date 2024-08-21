/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "strfuncs.h"
#include "md5.h"
#include "hostpid.h"
#include "str-sanitize.h"
#include "ostream.h"
#include "message-address.h"
#include "message-date.h"
#include "var-expand.h"
#include "ioloop.h"
#include "mail-storage.h"

#include "rfc2822.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-address.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"
#include "sieve-message.h"
#include "sieve-smtp.h"

#include "ext-vacation-common.h"

#include <stdio.h>

/*
 * Forward declarations
 */

static const struct sieve_argument_def vacation_days_tag;
static const struct sieve_argument_def vacation_subject_tag;
static const struct sieve_argument_def vacation_from_tag;
static const struct sieve_argument_def vacation_addresses_tag;
static const struct sieve_argument_def vacation_mime_tag;
static const struct sieve_argument_def vacation_handle_tag;

/*
 * Vacation command
 *
 * Syntax:
 *    vacation [":days" number] [":subject" string]
 *                 [":from" string] [":addresses" string-list]
 *                 [":mime"] [":handle" string] <reason: string>
 */

static bool
cmd_vacation_registered(struct sieve_validator *valdtr,
			const struct sieve_extension *ext,
			struct sieve_command_registration *cmd_reg);
static bool
cmd_vacation_pre_validate(struct sieve_validator *valdtr,
			  struct sieve_command *cmd);
static bool
cmd_vacation_validate(struct sieve_validator *valdtr,
		      struct sieve_command *cmd);
static bool
cmd_vacation_generate(const struct sieve_codegen_env *cgenv,
		      struct sieve_command *cmd);

const struct sieve_command_def vacation_command = {
	.identifier = "vacation",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = cmd_vacation_registered,
	.pre_validate = cmd_vacation_pre_validate,
	.validate = cmd_vacation_validate,
	.generate = cmd_vacation_generate,
};

/*
 * Vacation command tags
 */

/* Forward declarations */

static bool
cmd_vacation_validate_number_tag(struct sieve_validator *valdtr,
				 struct sieve_ast_argument **arg,
				 struct sieve_command *cmd);
static bool
cmd_vacation_validate_string_tag(struct sieve_validator *valdtr,
				 struct sieve_ast_argument **arg,
				 struct sieve_command *cmd);
static bool
cmd_vacation_validate_stringlist_tag(struct sieve_validator *valdtr,
				     struct sieve_ast_argument **arg,
				     struct sieve_command *cmd);
static bool
cmd_vacation_validate_mime_tag(struct sieve_validator *valdtr,
			       struct sieve_ast_argument **arg,
			       struct sieve_command *cmd);

/* Argument objects */

static const struct sieve_argument_def vacation_days_tag = {
	.identifier = "days",
	.validate = cmd_vacation_validate_number_tag,
};

static const struct sieve_argument_def vacation_seconds_tag = {
	.identifier = "seconds",
	.validate = cmd_vacation_validate_number_tag,
};

static const struct sieve_argument_def vacation_subject_tag = {
	.identifier = "subject",
	.validate = cmd_vacation_validate_string_tag,
};

static const struct sieve_argument_def vacation_from_tag = {
	.identifier = "from",
	.validate = cmd_vacation_validate_string_tag,
};

static const struct sieve_argument_def vacation_addresses_tag = {
	.identifier = "addresses",
	.validate = cmd_vacation_validate_stringlist_tag,
};

static const struct sieve_argument_def vacation_mime_tag = {
	.identifier = "mime",
	.validate = cmd_vacation_validate_mime_tag,
};

static const struct sieve_argument_def vacation_handle_tag = {
	.identifier = "handle",
	.validate = cmd_vacation_validate_string_tag,
};

/* Codes for optional arguments */

enum cmd_vacation_optional {
	OPT_END,
	OPT_SECONDS,
	OPT_SUBJECT,
	OPT_FROM,
	OPT_ADDRESSES,
	OPT_MIME,
};

/*
 * Vacation operation
 */

static bool
ext_vacation_operation_dump(const struct sieve_dumptime_env *denv,
			    sieve_size_t *address);
static int
ext_vacation_operation_execute(const struct sieve_runtime_env *renv,
			       sieve_size_t *address);

const struct sieve_operation_def vacation_operation = {
	.mnemonic = "VACATION",
	.ext_def = &vacation_extension,
	.dump = ext_vacation_operation_dump,
	.execute = ext_vacation_operation_execute,
};

/*
 * Vacation action
 */

/* Forward declarations */

static int
act_vacation_check_duplicate(const struct sieve_runtime_env *renv,
			     const struct sieve_action *act,
			     const struct sieve_action *act_other);
int act_vacation_check_conflict(const struct sieve_runtime_env *renv,
				const struct sieve_action *act,
				const struct sieve_action *act_other);
static void
act_vacation_print(const struct sieve_action *action,
		   const struct sieve_result_print_env *rpenv, bool *keep);
static int
act_vacation_commit(const struct sieve_action_exec_env *aenv, void *tr_context);

/* Action object */

const struct sieve_action_def act_vacation = {
	.name = "vacation",
	.flags = SIEVE_ACTFLAG_SENDS_RESPONSE,
	.check_duplicate = act_vacation_check_duplicate,
	.check_conflict = act_vacation_check_conflict,
	.print = act_vacation_print,
	.commit = act_vacation_commit,
};

/* Action context information */

struct act_vacation_context {
	const char *reason;

	sieve_number_t seconds;
	const char *subject;
	const char *handle;
	bool mime;
	const char *from;
	const struct smtp_address *from_address;
	const struct smtp_address *const *addresses;
};

/*
 * Command validation context
 */

struct cmd_vacation_context_data {
	string_t *from;
	string_t *subject;

	bool mime;

	struct sieve_ast_argument *handle_arg;
};

/*
 * Tag validation
 */

static bool
cmd_vacation_validate_number_tag(struct sieve_validator *valdtr,
				 struct sieve_ast_argument **arg,
				 struct sieve_command *cmd)
{
	const struct sieve_extension *ext = sieve_argument_ext(*arg);
	const struct ext_vacation_context *extctx = ext->context;
	struct sieve_ast_argument *tag = *arg;
	sieve_number_t period, seconds;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Check syntax:
	 *   :days number
	 */
	if (!sieve_validate_tag_parameter(valdtr, cmd, tag, *arg, NULL, 0,
					  SAAT_NUMBER, FALSE))
		return FALSE;

	period = sieve_ast_argument_number(*arg);
	if (sieve_argument_is(tag, vacation_days_tag))
		seconds = period * (24*60*60);
	else if (sieve_argument_is(tag, vacation_seconds_tag))
		seconds = period;
	else
		i_unreached();

	/* Enforce :seconds >= min_period */
	if (seconds < extctx->set->min_period) {
		seconds = extctx->set->min_period;

		sieve_argument_validate_warning(
			valdtr, *arg,
			"specified :%s value '%llu' is under the minimum",
			sieve_argument_identifier(tag),
			(unsigned long long)period);
	/* Enforce :days <= max_period */
	} else if (extctx->set->max_period > 0 &&
		   seconds > extctx->set->max_period) {
		seconds = extctx->set->max_period;

		sieve_argument_validate_warning(
			valdtr, *arg,
			"specified :%s value '%llu' is over the maximum",
			sieve_argument_identifier(tag),
			(unsigned long long)period);
	}

	sieve_ast_argument_number_set(*arg, seconds);

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool
cmd_vacation_validate_string_tag(struct sieve_validator *valdtr,
				 struct sieve_ast_argument **arg,
				 struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct cmd_vacation_context_data *ctx_data =
		(struct cmd_vacation_context_data *)cmd->data;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Check syntax:
	 *   :subject string
	 *   :from string
	 *   :handle string
	 */
	if (!sieve_validate_tag_parameter(valdtr, cmd, tag, *arg, NULL, 0,
					  SAAT_STRING, FALSE))
		return FALSE;

	if (sieve_argument_is(tag, vacation_from_tag)) {
		if (sieve_argument_is_string_literal(*arg)) {
			string_t *address = sieve_ast_argument_str(*arg);
			const char *error;
	 		bool result;

	 		T_BEGIN {
				result = sieve_address_validate_str(address,
								    &error);

				if (!result) {
					sieve_argument_validate_error(
						valdtr, *arg,
						"specified :from address '%s' is invalid for vacation action: %s",
						str_sanitize(str_c(address), 128),
						error);
				}
			} T_END;

			if (!result)
				return FALSE;
		}

		ctx_data->from = sieve_ast_argument_str(*arg);

		/* Skip parameter */
		*arg = sieve_ast_argument_next(*arg);
	} else if (sieve_argument_is(tag, vacation_subject_tag)) {
		ctx_data->subject = sieve_ast_argument_str(*arg);

		/* Skip parameter */
		*arg = sieve_ast_argument_next(*arg);
	} else if (sieve_argument_is(tag, vacation_handle_tag)) {
		ctx_data->handle_arg = *arg;

		/* Detach optional argument (emitted as mandatory) */
		*arg = sieve_ast_arguments_detach(*arg, 1);
	}
	return TRUE;
}

static bool
cmd_vacation_validate_stringlist_tag(struct sieve_validator *valdtr,
				     struct sieve_ast_argument **arg,
				     struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Check syntax:
	 *   :addresses string-list
	 */
	if (!sieve_validate_tag_parameter(valdtr, cmd, tag, *arg, NULL, 0,
					  SAAT_STRING_LIST, FALSE))
		return FALSE;

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool
cmd_vacation_validate_mime_tag(struct sieve_validator *valdtr ATTR_UNUSED,
			       struct sieve_ast_argument **arg,
			       struct sieve_command *cmd)
{
	struct cmd_vacation_context_data *ctx_data =
		(struct cmd_vacation_context_data *)cmd->data;

	ctx_data->mime = TRUE;

	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

/*
 * Command registration
 */

static bool
cmd_vacation_registered(struct sieve_validator *valdtr,
			const struct sieve_extension *ext,
			struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &vacation_days_tag, OPT_SECONDS);
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &vacation_subject_tag, OPT_SUBJECT);
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &vacation_from_tag, OPT_FROM);
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &vacation_addresses_tag, OPT_ADDRESSES);
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &vacation_mime_tag, OPT_MIME);
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &vacation_handle_tag, 0);
	return TRUE;
}

bool ext_vacation_register_seconds_tag(
	struct sieve_validator *valdtr,
	const struct sieve_extension *vacation_ext)
{
	sieve_validator_register_external_tag(
		valdtr, vacation_command.identifier, vacation_ext,
		&vacation_seconds_tag, OPT_SECONDS);

	return TRUE;
}

/*
 * Command validation
 */

static bool
cmd_vacation_pre_validate(struct sieve_validator *valdtr ATTR_UNUSED,
			  struct sieve_command *cmd)
{
	struct cmd_vacation_context_data *ctx_data;

	/* Assign context */
	ctx_data = p_new(sieve_command_pool(cmd),
		struct cmd_vacation_context_data, 1);
	cmd->data = ctx_data;

	return TRUE;
}

static const char _handle_empty_subject[] = "<default-subject>";
static const char _handle_empty_from[] = "<default-from>";
static const char _handle_mime_enabled[] = "<MIME>";
static const char _handle_mime_disabled[] = "<NO-MIME>";

static bool
cmd_vacation_validate(struct sieve_validator *valdtr,
		      struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct cmd_vacation_context_data *ctx_data =
		(struct cmd_vacation_context_data *)cmd->data;

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "reason", 1,
						SAAT_STRING))
		return FALSE;

	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;

	/* Construct handle if not set explicitly */
	if (ctx_data->handle_arg == NULL) {
		T_BEGIN {
			string_t *handle;
			string_t *reason = sieve_ast_argument_str(arg);
			unsigned int size = str_len(reason);

			/* Precalculate the size of it all */
			size += (ctx_data->subject == NULL ?
				 sizeof(_handle_empty_subject) - 1 :
				 str_len(ctx_data->subject));
			size += (ctx_data->from == NULL ?
				 sizeof(_handle_empty_from) - 1 :
				 str_len(ctx_data->from));
			size += (ctx_data->mime ?
				 sizeof(_handle_mime_enabled) - 1 :
				 sizeof(_handle_mime_disabled) - 1);

			/* Construct the string */
			handle = t_str_new(size);
			str_append_str(handle, reason);

			if (ctx_data->subject != NULL)
				str_append_str(handle, ctx_data->subject);
			else
				str_append(handle, _handle_empty_subject);

			if (ctx_data->from != NULL)
				str_append_str(handle, ctx_data->from);
			else
				str_append(handle, _handle_empty_from);

			str_append(handle, (ctx_data->mime ?
					    _handle_mime_enabled :
					    _handle_mime_disabled));

			/* Create positional handle argument */
			ctx_data->handle_arg =
				sieve_ast_argument_string_create(
					cmd->ast_node, handle,
					sieve_ast_node_line(cmd->ast_node));
		} T_END;

		if (!sieve_validator_argument_activate(
			valdtr, cmd, ctx_data->handle_arg, TRUE))
			return FALSE;
	} else {
		/* Attach explicit handle argument as positional */
		(void)sieve_ast_argument_attach(cmd->ast_node,
						ctx_data->handle_arg);
	}

	return TRUE;
}

/*
 * Code generation
 */

static bool
cmd_vacation_generate(const struct sieve_codegen_env *cgenv,
		      struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &vacation_operation);

	/* Generate arguments */
	if (!sieve_generate_arguments(cgenv, cmd, NULL))
		return FALSE;
	return TRUE;
}

/*
 * Code dump
 */

static bool
ext_vacation_operation_dump(const struct sieve_dumptime_env *denv,
			    sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "VACATION");
	sieve_code_descend(denv);

	/* Dump optional operands */

	for (;;) {
		int opt;
		bool opok = TRUE;

		if ((opt = sieve_opr_optional_dump(denv, address,
						   &opt_code)) < 0)
			return FALSE;

		if (opt == 0)
			break;

		switch (opt_code) {
		case OPT_SECONDS:
			opok = sieve_opr_number_dump(denv, address, "seconds");
			break;
		case OPT_SUBJECT:
			opok = sieve_opr_string_dump(denv, address, "subject");
			break;
		case OPT_FROM:
			opok = sieve_opr_string_dump(denv, address, "from");
			break;
		case OPT_ADDRESSES:
			opok = sieve_opr_stringlist_dump(denv, address,
							 "addresses");
			break;
		case OPT_MIME:
			sieve_code_dumpf(denv, "mime");
			break;
		default:
			return FALSE;
		}

		if (!opok)
			return FALSE;
	}

	/* Dump reason and handle operands */
	return (sieve_opr_string_dump(denv, address, "reason") &&
		sieve_opr_string_dump(denv, address, "handle"));
}

/*
 * Code execution
 */

static int
ext_vacation_operation_execute(const struct sieve_runtime_env *renv,
			       sieve_size_t *address)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	const struct ext_vacation_context *extctx = this_ext->context;
	struct sieve_side_effects_list *slist = NULL;
	struct act_vacation_context *act;
	pool_t pool;
	int opt_code = 0;
	sieve_number_t seconds = extctx->set->default_period;
	bool mime = FALSE;
	struct sieve_stringlist *addresses = NULL;
	string_t *reason, *subject = NULL, *from = NULL, *handle = NULL;
	const struct smtp_address *from_address = NULL;
	int ret;

	/*
	 * Read code
	 */

	/* Optional operands */

	for (;;) {
		int opt;

		if ((opt = sieve_opr_optional_read(renv, address,
						   &opt_code)) < 0)
			return SIEVE_EXEC_BIN_CORRUPT;

		if (opt == 0)
			break;

		switch (opt_code) {
		case OPT_SECONDS:
			ret = sieve_opr_number_read(renv, address, "seconds",
						    &seconds);
			break;
		case OPT_SUBJECT:
			ret = sieve_opr_string_read(renv, address, "subject",
						    &subject);
			break;
		case OPT_FROM:
			ret = sieve_opr_string_read(renv, address, "from",
						    &from);
			break;
		case OPT_ADDRESSES:
			ret = sieve_opr_stringlist_read(renv, address,
							"addresses",
							&addresses);
			break;
		case OPT_MIME:
			mime = TRUE;
			ret = SIEVE_EXEC_OK;
			break;
		default:
			sieve_runtime_trace_error(
				renv, "unknown optional operand");
			ret = SIEVE_EXEC_BIN_CORRUPT;
		}

		if (ret <= 0)
			return ret;
	}

	/* Fixed operands */

	ret = sieve_opr_string_read(renv, address, "reason", &reason);
	if (ret <= 0)
		return ret;
	ret = sieve_opr_string_read(renv, address, "handle", &handle);
	if (ret <= 0)
		return ret;

	/*
	 * Perform operation
	 */

	/* Trace */

	if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_ACTIONS)) {
		sieve_runtime_trace(renv, 0, "vacation action");
		sieve_runtime_trace_descend(renv);
		sieve_runtime_trace(renv, 0, "auto-reply with message '%s'",
				    str_sanitize(str_c(reason), 80));
	}

	/* Parse :from address */
	if (from != NULL) {
		const char *error;

		from_address = sieve_address_parse_str(from, &error);
		if (from_address == NULL) {
			sieve_runtime_error(
				renv, NULL,
				"specified :from address '%s' is invalid for vacation action: %s",
				str_sanitize(str_c(from), 128), error);
   		}
	}

	/* Add vacation action to the result */

	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_vacation_context, 1);
	act->reason = p_strdup(pool, str_c(reason));
	act->handle = p_strdup(pool, str_c(handle));
	act->seconds = seconds;
	act->mime = mime;
	if (subject != NULL)
		act->subject = p_strdup(pool, str_c(subject));
	if (from != NULL) {
		act->from = p_strdup(pool, str_c(from));
		act->from_address = smtp_address_clone(pool, from_address);
	}

	/* Normalize all addresses */
	if (addresses != NULL) {
		ARRAY_TYPE(smtp_address_const) addrs;
		string_t *raw_address;
		int ret;

		sieve_stringlist_reset(addresses);

		p_array_init(&addrs, pool, 4);

		raw_address = NULL;
		while ((ret = sieve_stringlist_next_item(addresses,
							 &raw_address)) > 0) {
			const struct smtp_address *addr;
			const char *error;

			addr = sieve_address_parse_str(raw_address, &error);
			if (addr != NULL) {
				addr = smtp_address_clone(pool, addr);
				array_append(&addrs, &addr, 1);
			} else {
				sieve_runtime_error(
					renv, NULL,
					"specified :addresses item '%s' is invalid: "
					"%s for vacation action (ignored)",
					str_sanitize(str_c(raw_address),128),
					error);
			}
		}

		if (ret < 0) {
			sieve_runtime_trace_error(
				renv, "invalid addresses stringlist");
			return SIEVE_EXEC_BIN_CORRUPT;
		}

		(void)array_append_space(&addrs);
		act->addresses = array_idx(&addrs, 0);
	}

	if (sieve_result_add_action(renv, this_ext, "vacation", &act_vacation,
				    slist, act, 0, FALSE) < 0)
		return SIEVE_EXEC_FAILURE;

	return SIEVE_EXEC_OK;
}

/*
 * Action
 */

/* Runtime verification */

static int
act_vacation_check_duplicate(const struct sieve_runtime_env *renv ATTR_UNUSED,
			     const struct sieve_action *act,
			     const struct sieve_action *act_other)
{
	if (!sieve_action_is_executed(act_other, renv->result)) {
		sieve_runtime_error(
			renv, act->location,
			"duplicate vacation action not allowed "
			"(previously triggered one was here: %s)",
			act_other->location);
		return -1;
	}

	/* Not an error if executed in preceeding script */
	return 1;
}

int act_vacation_check_conflict(const struct sieve_runtime_env *renv,
				const struct sieve_action *act,
				const struct sieve_action *act_other)
{
	if ((act_other->def->flags & SIEVE_ACTFLAG_SENDS_RESPONSE) > 0) {
		if (!sieve_action_is_executed(act_other, renv->result)) {
			sieve_runtime_error(
				renv, act->location,
				"vacation action conflicts with other action: "
				"the %s action (%s) also sends a response back to the sender",
				act_other->def->name, act_other->location);
			return -1;
		} else {
			/* Not an error if executed in preceeding script */
			return 1;
		}
	}

	return 0;
}

/* Result printing */

static void act_vacation_print(const struct sieve_action *action ATTR_UNUSED,
			       const struct sieve_result_print_env *rpenv,
			       bool *keep ATTR_UNUSED)
{
	struct act_vacation_context *ctx =
		(struct act_vacation_context *)action->context;

	sieve_result_action_printf(rpenv, "send vacation message:");
	sieve_result_printf(rpenv, "    => seconds : %llu\n",
			    (unsigned long long)ctx->seconds);
	if (ctx->subject != NULL) {
		sieve_result_printf(rpenv, "    => subject : %s\n",
				    ctx->subject);
	}
	if (ctx->from != NULL) {
		sieve_result_printf(rpenv, "    => from    : %s\n",
				    ctx->from);
	}
	if (ctx->handle != NULL) {
		sieve_result_printf(rpenv, "    => handle  : %s\n",
				    ctx->handle);
	}
	sieve_result_printf(rpenv, "\nSTART MESSAGE\n%s\nEND MESSAGE\n",
			    ctx->reason);
}

/* Result execution */

/* Headers known to be associated with mailing lists
 */
static const char *const _list_headers[] = {
	"list-id",
	"list-owner",
	"list-subscribe",
	"list-post",
	"list-unsubscribe",
	"list-help",
	"list-archive",
	NULL
};

/* Headers that should be searched for the user's own mail address(es)
 */

static const char *const _my_address_headers[] = {
	"to",
	"cc",
	"bcc",
	"resent-to",
	"resent-cc",
	"resent-bcc",
	NULL
};

/* Headers that should be searched for the full sender address
 */

static const char *const _sender_headers[] = {
	"sender",
	"resent-from",
	"from",
	NULL
};

static inline bool _is_system_address(const struct smtp_address *address)
{
	if (strcasecmp(address->localpart, "MAILER-DAEMON") == 0)
		return TRUE;
	if (strcasecmp(address->localpart, "LISTSERV") == 0)
		return TRUE;
	if (strcasecmp(address->localpart, "majordomo") == 0)
		return TRUE;
	if (strstr(address->localpart, "-request") != NULL)
		return TRUE;
	if (str_begins_with(address->localpart, "owner-"))
		return TRUE;
	return FALSE;
}

static bool
_msg_address_equals(const struct message_address *addr1,
		    const struct smtp_address *addr2)
{
	struct smtp_address saddr;

	i_assert(addr1->mailbox != NULL);
	return (smtp_address_init_from_msg(&saddr, addr1) >= 0 &&
		smtp_address_equals_icase(addr2, &saddr));
}

static inline bool
_header_contains_my_address(const char *header_val,
			    const struct smtp_address *my_address)
{
	const struct message_address *msg_addr;

	msg_addr = message_address_parse(pool_datastack_create(),
					 (const unsigned char *)header_val,
					 strlen(header_val), 256, 0);
	while (msg_addr != NULL) {
		if (msg_addr->domain != NULL) {
			if (_msg_address_equals(msg_addr, my_address))
				return TRUE;
		}

		msg_addr = msg_addr->next;
	}

	return FALSE;
}

static inline bool
_contains_my_address(const char *const *headers,
		     const struct smtp_address *my_address)
{
	const char *const *hdsp = headers;

	while (*hdsp != NULL) {
		bool result;

		T_BEGIN {
			result = _header_contains_my_address(*hdsp, my_address);
		} T_END;

		if (result)
			return TRUE;

		hdsp++;
	}

	return FALSE;
}

static bool _contains_8bit(const char *text)
{
	const unsigned char *p = (const unsigned char *)text;

	for (; *p != '\0'; p++) {
		if ((*p & 0x80) != 0)
			return TRUE;
	}
	return FALSE;
}

static bool
_header_get_full_reply_recipient(const struct ext_vacation_context *extctx,
				 const struct smtp_address *smtp_to,
				 const char *header,
				 struct message_address *reply_to_r)
{
	const struct message_address *addr;

	addr = message_address_parse(
		pool_datastack_create(),
		(const unsigned char *)header,
		strlen(header), 256, 0);

	for (; addr != NULL; addr = addr->next) {
		bool matched = extctx->set->to_header_ignore_envelope;

		if (addr->domain == NULL || addr->invalid_syntax)
			continue;

		if (!matched)
			matched = _msg_address_equals(addr, smtp_to);

		if (matched) {
			*reply_to_r = *addr;
			return TRUE;
		}
	}
	return FALSE;
}

static int
_get_full_reply_recipient(const struct sieve_action_exec_env *aenv,
			  const struct ext_vacation_context *extctx,
			  const struct smtp_address *smtp_to,
			  struct message_address *reply_to_r)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	const struct sieve_message_data *msgdata = eenv->msgdata;
	const char *const *hdsp;
	int ret;

	hdsp = _sender_headers;
	for (; *hdsp != NULL; hdsp++) {
		const char *header;

		ret = mail_get_first_header(msgdata->mail, *hdsp, &header);
		if (ret < 0) {
			return sieve_result_mail_error(
				aenv, msgdata->mail,
				"failed to read header field '%s'", *hdsp);
		}
		if (ret == 0 || header == NULL)
			continue;

		if (_header_get_full_reply_recipient(extctx, smtp_to,
						     header, reply_to_r))
			return SIEVE_EXEC_OK;
	}

	reply_to_r->mailbox = smtp_to->localpart;
	reply_to_r->domain = smtp_to->domain;
	return SIEVE_EXEC_OK;
}

static const struct var_expand_table *
_get_var_expand_table(const struct sieve_action_exec_env *aenv ATTR_UNUSED,
		      const char *subject)
{
	const struct var_expand_table stack_tab[] = {
		{ .key = "subject", .value = subject },
		VAR_EXPAND_TABLE_END
	};

	return p_memdup(unsafe_data_stack_pool, stack_tab, sizeof(stack_tab));
}

static int
act_vacation_get_default_subject(const struct sieve_action_exec_env *aenv,
				 const struct ext_vacation_context *extctx,
				 const char **subject_r)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	const struct sieve_message_data *msgdata = eenv->msgdata;
	const char *header, *error;
	string_t *str;
	int ret;

	*subject_r = (*extctx->set->default_subject == '\0' ?
		      "Automated reply" : extctx->set->default_subject);
	ret = mail_get_first_header_utf8(msgdata->mail, "subject", &header);
	if (ret < 0) {
		return sieve_result_mail_error(
			aenv, msgdata->mail,
			"failed to read header field 'subject'");
	}
	if (ret == 0)
		return SIEVE_EXEC_OK;
	if (*extctx->set->default_subject_template == '\0') {
		*subject_r = t_strconcat("Auto: ", header, NULL);
		return SIEVE_EXEC_OK;
	}

	str = t_str_new(256);
	const struct var_expand_params params = {
		.table = _get_var_expand_table(aenv, header),
	};
	if (var_expand(str, extctx->set->default_subject_template, &params,
		       &error) < 0) {
		e_error(aenv->event,
			"Failed to expand deliver_log_format=%s: %s",
			extctx->set->default_subject_template, error);
		*subject_r = t_strconcat("Auto: ", header, NULL);
		return SIEVE_EXEC_OK;
	}

	*subject_r = str_c(str);
	return SIEVE_EXEC_OK;
}

static int
act_vacation_send(const struct sieve_action_exec_env *aenv,
		  const struct ext_vacation_context *extctx,
		  struct act_vacation_context *actx,
		  const struct smtp_address *smtp_to,
		  const struct smtp_address *smtp_from,
		  const struct message_address *reply_from)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	const struct sieve_message_data *msgdata = eenv->msgdata;
	const struct sieve_script_env *senv = eenv->scriptenv;
	struct sieve_smtp_context *sctx;
	struct ostream *output;
	string_t *msg;
	struct message_address reply_to;
	const char *header, *outmsgid, *subject, *error;
	int ret;

	/* Check smpt functions just to be sure */

	if (!sieve_smtp_available(senv)) {
		sieve_result_global_warning(
			aenv, "vacation action has no means to send mail");
		return SIEVE_EXEC_OK;
	}

	/* Make sure we have a subject for our reply */

	if (actx->subject == NULL || *(actx->subject) == '\0') {
		ret = act_vacation_get_default_subject(aenv, extctx, &subject);
		if (ret <= 0)
			return ret;
	} else {
		subject = actx->subject;
	}

	subject = str_sanitize_utf8(
		subject, SIEVE_MAX_SUBJECT_HEADER_CODEPOINTS);

	/* Obtain full To address for reply */

	i_zero(&reply_to);
	reply_to.mailbox = smtp_to->localpart;
	reply_to.domain = smtp_to->domain;
	ret = _get_full_reply_recipient(aenv, extctx, smtp_to, &reply_to);
	if (ret <= 0)
		return ret;

	/* Open smtp session */

	sctx = sieve_smtp_start_single(senv, smtp_to, smtp_from, &output);

	outmsgid = sieve_message_get_new_id(eenv->svinst);

	/* Produce a proper reply */

	msg = t_str_new(512);
	rfc2822_header_write(msg, "X-Sieve", SIEVE_IMPLEMENTATION);
	rfc2822_header_write(msg, "Message-ID", outmsgid);
	rfc2822_header_write(msg, "Date", message_date_create(ioloop_time));

	if (actx->from != NULL && *(actx->from) != '\0') {
		rfc2822_header_write_address(msg, "From", actx->from);
	} else {
		if (reply_from == NULL || reply_from->mailbox == NULL ||
		    *reply_from->mailbox == '\0')
			reply_from = sieve_get_postmaster(senv);
		rfc2822_header_write(
			msg, "From",
			message_address_first_to_string(reply_from));
	}

	rfc2822_header_write(msg, "To",
			     message_address_first_to_string(&reply_to));

	if (_contains_8bit(subject))
		rfc2822_header_utf8_printf(msg, "Subject", "%s", subject);
	else
		rfc2822_header_printf(msg, "Subject", "%s", subject);

	/* Compose proper in-reply-to and references headers */

	ret = mail_get_first_header(msgdata->mail, "references", &header);
	if (ret < 0) {
		sieve_smtp_abort(sctx);
		return sieve_result_mail_error(
			aenv, msgdata->mail,
			"failed to read header field 'references'");
	}

	if (msgdata->id != NULL) {
		rfc2822_header_write(msg, "In-Reply-To", msgdata->id);

		if (ret > 0 && header != NULL) {
			rfc2822_header_write(
				msg, "References",
				t_strconcat(header, " ", msgdata->id, NULL));
		} else {
			rfc2822_header_write(msg, "References", msgdata->id);
		}
	} else if (ret > 0 && header != NULL) {
		rfc2822_header_write(msg, "References", header);
	}

	rfc2822_header_write(msg, "Auto-Submitted", "auto-replied (vacation)");
	rfc2822_header_write(msg, "Precedence", "bulk");

	/* Prevent older Microsoft products from replying to this message */
	rfc2822_header_write(msg, "X-Auto-Response-Suppress", "All");

	rfc2822_header_write(msg, "MIME-Version", "1.0");

	if (!actx->mime) {
		rfc2822_header_write(msg, "Content-Type",
				     "text/plain; charset=utf-8");
		rfc2822_header_write(msg, "Content-Transfer-Encoding", "8bit");
		str_append(msg, "\r\n");
	}

	str_printfa(msg, "%s\r\n", actx->reason);
	o_stream_nsend(output, str_data(msg), str_len(msg));

	/* Close smtp session */
	ret = sieve_smtp_finish(sctx, &error);
	if (ret <= 0) {
		if (ret < 0) {
			sieve_result_global_error(
				aenv, "failed to send vacation response to %s: "
				"<%s> (temporary error)",
				smtp_address_encode(smtp_to),
				str_sanitize(error, 512));
		} else {
			sieve_result_global_log_error(
				aenv, "failed to send vacation response to %s: "
				"<%s> (permanent error)",
				smtp_address_encode(smtp_to),
				str_sanitize(error, 512));
		}
		/* This error will be ignored in the end */
		return SIEVE_EXEC_FAILURE;
	}

	eenv->exec_status->significant_action_executed = TRUE;
	return SIEVE_EXEC_OK;
}

static void
act_vacation_hash(struct act_vacation_context *vctx, const char *sender,
		  unsigned char hash_r[])
{
	const char *rpath = t_str_lcase(sender);
	struct md5_context ctx;

	md5_init(&ctx);
	md5_update(&ctx, rpath, strlen(rpath));

	md5_update(&ctx, vctx->handle, strlen(vctx->handle));

	md5_final(&ctx, hash_r);
}

static int
act_vacation_commit(const struct sieve_action_exec_env *aenv,
		    void *tr_context ATTR_UNUSED)
{
	const struct sieve_action *action = aenv->action;
	const struct sieve_extension *ext = action->ext;
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_instance *svinst = eenv->svinst;
	const struct ext_vacation_context *extctx = ext->context;
	struct act_vacation_context *actx = action->context;
	unsigned char dupl_hash[MD5_RESULTLEN];
	struct mail *mail = sieve_message_get_mail(aenv->msgctx);
	const struct smtp_address *sender, *recipient;
	const struct smtp_address *orig_recipient, *user_email;
	const struct smtp_address *smtp_from;
	struct message_address reply_from;
	const char *const *hdsp, *const *headers;
	int ret;

	if ((eenv->flags & SIEVE_EXECUTE_FLAG_SKIP_RESPONSES) != 0) {
		sieve_result_global_log(
			aenv, "not sending vacation reply (skipped)");
		return SIEVE_EXEC_OK;
	}

	sender = sieve_message_get_sender(aenv->msgctx);
	recipient = sieve_message_get_final_recipient(aenv->msgctx);

	i_zero(&reply_from);
	smtp_from = orig_recipient = user_email = NULL;

	/* Is the recipient unset?
	 */
	if (smtp_address_isnull(recipient)) {
		sieve_result_global_warning(
			aenv, "vacation action aborted: "
			"envelope recipient is <>");
		return SIEVE_EXEC_OK;
	}

	/* Is the return path unset ?
	 */
	if (smtp_address_isnull(sender)) {
		sieve_result_global_log(aenv, "discarded vacation reply to <>");
		return SIEVE_EXEC_OK;
	}

	/* Are we perhaps trying to respond to ourselves ?
	 */
	if (smtp_address_equals_icase(sender, recipient)) {
		sieve_result_global_log(
			aenv, "discarded vacation reply to own address <%s>",
			smtp_address_encode(sender));
		return SIEVE_EXEC_OK;
	}

	/* Are we perhaps trying to respond to one of our alternative :addresses?
	 */
	if (actx->addresses != NULL) {
		const struct smtp_address *const *alt_address;

		alt_address = actx->addresses;
		while (*alt_address != NULL) {
			if (smtp_address_equals_icase(sender, *alt_address)) {
				sieve_result_global_log(
					aenv,
					"discarded vacation reply to own address <%s> "
					"(as specified using :addresses argument)",
					smtp_address_encode(sender));
				return SIEVE_EXEC_OK;
			}
			alt_address++;
		}
	}

	/* Did whe respond to this user before? */
	if (sieve_action_duplicate_check_available(aenv)) {
		bool duplicate;

		act_vacation_hash(actx, smtp_address_encode(sender), dupl_hash);

		ret = sieve_action_duplicate_check(aenv, dupl_hash,
						   sizeof(dupl_hash),
						   &duplicate);
		if (ret < SIEVE_EXEC_OK) {
			sieve_result_critical(
				aenv, "failed to check for duplicate vacation response",
				"failed to check for duplicate vacation response%s",
				(ret == SIEVE_EXEC_TEMP_FAILURE ?
				 " (temporaty failure)" : ""));
			return ret;
		}
		if (duplicate) {
			sieve_result_global_log(
				aenv,
				"discarded duplicate vacation response to <%s>",
				smtp_address_encode(sender));
			return SIEVE_EXEC_OK;
		}
	}

	/* Are we trying to respond to a mailing list ? */
	hdsp = _list_headers;
	while (*hdsp != NULL) {
		ret = mail_get_headers(mail, *hdsp, &headers);
		if (ret < 0) {
			return sieve_result_mail_error(
				aenv, mail,
				"failed to read header field '%s'", *hdsp);
		}

		if (ret > 0 && headers[0] != NULL) {
			/* Yes, bail out */
			sieve_result_global_log(
				aenv, "discarding vacation response "
				"to mailinglist recipient <%s>",
				smtp_address_encode(sender));
			return SIEVE_EXEC_OK;
		}
		hdsp++;
	}

	/* Is the message that we are replying to an automatic reply ? */
	ret = mail_get_headers(mail, "auto-submitted", &headers);
	if (ret < 0) {
		return sieve_result_mail_error(
			aenv, mail,
			"failed to read header field 'auto-submitted'");
	}
	/* Theoretically multiple headers could exist, so lets make sure */
	if (ret > 0) {
		hdsp = headers;
		while (*hdsp != NULL) {
			if (strcasecmp(*hdsp, "no") != 0) {
				sieve_result_global_log(
					aenv, "discarding vacation response "
					"to auto-submitted message from <%s>",
					smtp_address_encode(sender));
					return SIEVE_EXEC_OK;
			}
			hdsp++;
		}
	}

	/* Check for the (non-standard) precedence header */
	ret = mail_get_headers(mail, "precedence", &headers);
	if (ret < 0) {
		return sieve_result_mail_error(
			aenv, mail, "failed to read header field 'precedence'");
	}
	/* Theoretically multiple headers could exist, so lets make sure */
	if (ret > 0) {
		hdsp = headers;
		while (*hdsp != NULL) {
			if (strcasecmp(*hdsp, "junk") == 0 ||
			    strcasecmp(*hdsp, "bulk") == 0 ||
			    strcasecmp(*hdsp, "list") == 0) {
				sieve_result_global_log(
					aenv, "discarding vacation response "
					"to precedence=%s message from <%s>",
					*hdsp, smtp_address_encode(sender));
					return SIEVE_EXEC_OK;
			}
			hdsp++;
		}
	}

	/* Check for the (non-standard) Microsoft X-Auto-Response-Suppress header */
	ret = mail_get_headers(mail, "x-auto-response-suppress", &headers);
	if (ret < 0) {
		return sieve_result_mail_error(
			aenv, mail,
			"failed to read header field 'x-auto-response-suppress'");
	}
	/* Theoretically multiple headers could exist, so lets make sure */
	if (ret > 0) {
		hdsp = headers;
		while (*hdsp != NULL) {
			const char *const *flags = t_strsplit(*hdsp, ",");

			while (*flags != NULL) {
				const char *flag = t_str_trim(*flags, " \t");

				if (strcasecmp(flag, "All") == 0 ||
				    strcasecmp(flag, "OOF") == 0) {
					sieve_result_global_log(
						aenv, "discarding vacation response to message from <%s> "
						"('%s' flag found in x-auto-response-suppress header)",
						smtp_address_encode(sender), flag);
					return SIEVE_EXEC_OK;
				}
				flags++;
			}
			hdsp++;
		}
	}

	/* Do not reply to system addresses */
	if (_is_system_address(sender)) {
		sieve_result_global_log(
			aenv, "not sending vacation response to system address <%s>",
			smtp_address_encode(sender));
		return SIEVE_EXEC_OK;
	}

	/* Fetch original recipient if necessary */
	if (extctx->set->use_original_recipient)
		orig_recipient = sieve_message_get_orig_recipient(aenv->msgctx);
	/* Fetch explicitly configured user email address */
	if (svinst->set->parsed.user_email != NULL)
		user_email = svinst->set->parsed.user_email;

	/* Is the original message directly addressed to the user or the addresses
	 * specified using the :addresses tag?
	 */
	hdsp = _my_address_headers;
	while (*hdsp != NULL) {
		ret = mail_get_headers(mail, *hdsp, &headers);
		if (ret < 0) {
			return sieve_result_mail_error(
				aenv, mail, "failed to read header field '%s'",
				*hdsp);
		}
		if (ret > 0 && headers[0] != NULL) {
			/* Final recipient directly listed in headers? */
			if (_contains_my_address(headers, recipient)) {
				smtp_from = recipient;
				message_address_init_from_smtp(
					&reply_from, NULL, recipient);
				break;
			}

			/* Original recipient directly listed in headers? */
			if (!smtp_address_isnull(orig_recipient) &&
			    _contains_my_address(headers, orig_recipient)) {
				smtp_from = orig_recipient;
				message_address_init_from_smtp(
					&reply_from, NULL, orig_recipient);
				break;
			}

			/* User-provided :addresses listed in headers? */
			if (actx->addresses != NULL) {
				bool found = FALSE;
				const struct smtp_address *const *my_address;

				my_address = actx->addresses;
				while (!found && *my_address != NULL) {
					if ((found = _contains_my_address(headers, *my_address))) {
						/* Avoid letting user determine SMTP sender directly */
						smtp_from = (orig_recipient == NULL ?
							     recipient : orig_recipient);
						message_address_init_from_smtp(
							&reply_from, NULL, *my_address);
					}
					my_address++;
				}

				if (found) break;
			}

			/* Explicitly-configured user email address directly listed in
			   headers? */
			if (user_email != NULL &&
			    _contains_my_address(headers, user_email)) {
				smtp_from = user_email;
				message_address_init_from_smtp(
					&reply_from, NULL, smtp_from);
				break;
			}
		}
		hdsp++;
	}

	/* My address not found in the headers; we got an implicit delivery */
	if (*hdsp == NULL) {
		if (extctx->set->dont_check_recipient) {
			/* Send reply from envelope recipient address */
			smtp_from = (orig_recipient == NULL ?
				     recipient : orig_recipient);
			if (user_email == NULL)
				user_email = sieve_get_user_email(svinst);
			message_address_init_from_smtp(&reply_from,
						       NULL, user_email);
		} else {
			const char *orig_rcpt_str = "", *user_email_str = "";

			/* Bail out */
			if (extctx->set->use_original_recipient) {
				orig_rcpt_str =
					t_strdup_printf("original-recipient=<%s>, ",
							(orig_recipient == NULL ? "UNAVAILABLE" :
							 smtp_address_encode(orig_recipient)));
			}

			if (user_email != NULL) {
				user_email_str = t_strdup_printf(
					"user-email=<%s>, ",
					smtp_address_encode(user_email));
			}

			sieve_result_global_log(
				aenv, "discarding vacation response for implicitly delivered message; "
				"no known (envelope) recipient address found in message headers "
				"(recipient=<%s>, %s%sand%s additional ':addresses' are specified)",
				smtp_address_encode(recipient),
				orig_rcpt_str, user_email_str,
				(actx->addresses == NULL || *actx->addresses == NULL ?
				 " no" : ""));
			return SIEVE_EXEC_OK;
		}
	}

	/* Send the message */

	T_BEGIN {
		ret = act_vacation_send(
			aenv, extctx, actx, sender,
			(extctx->set->send_from_recipient ? smtp_from : NULL),
			&reply_from);
	} T_END;

	if (ret == SIEVE_EXEC_OK) {
		sieve_number_t seconds;

		eenv->exec_status->significant_action_executed = TRUE;

		struct event_passthrough *e =
			sieve_action_create_finish_event(aenv);

		sieve_result_event_log(aenv, e->event(),
				       "sent vacation response to <%s>",
				       smtp_address_encode(sender));

		/* Check period limits once more */
		seconds = actx->seconds;
		if (seconds < extctx->set->min_period)
			seconds = extctx->set->min_period;
		else if (extctx->set->max_period > 0 &&
			 seconds > extctx->set->max_period)
			seconds = extctx->set->max_period;

		/* Mark as replied */
		if (seconds > 0) {
			sieve_action_duplicate_mark(aenv, dupl_hash,
						    sizeof(dupl_hash),
						    ioloop_time + seconds);
		}
	}

	if (ret == SIEVE_EXEC_TEMP_FAILURE)
		return SIEVE_EXEC_TEMP_FAILURE;

	/* Ignore all other errors */
	return SIEVE_EXEC_OK;
}
