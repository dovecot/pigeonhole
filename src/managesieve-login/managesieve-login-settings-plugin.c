/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "config-parser-private.h"
#include "sieve.h"
#include "managesieve-login-settings-plugin.h"

static int
(*next_hook_config_parser_end)(struct config_parser_context *ctx,
			       struct config_parsed *new_config,
			       struct event *event, const char **error_r) = NULL;

static int
managesieve_login_config_parser_end(struct config_parser_context *ctx,
				    struct config_parsed *new_config,
				    struct event *event, const char **error_r);

const char *managesieve_login_settings_version = DOVECOT_ABI_VERSION;

void managesieve_login_settings_init(struct module *module ATTR_UNUSED)
{
	next_hook_config_parser_end = hook_config_parser_end;
	hook_config_parser_end = managesieve_login_config_parser_end;
}

void managesieve_login_settings_deinit(void)
{
	hook_config_parser_end = next_hook_config_parser_end;
}

static void
managesieve_login_config_set(struct config_parser_context *ctx,
			     const char *key, const char *value)
{
	config_parser_set_change_counter(ctx, CONFIG_PARSER_CHANGE_DEFAULTS);
	config_apply_line(ctx, key, value, NULL);
	config_parser_set_change_counter(ctx, CONFIG_PARSER_CHANGE_EXPLICIT);
}

static int
dump_capability(struct config_parser_context *ctx,
		struct config_parsed *new_config,
		struct event *event, const char **error_r)
{
	struct sieve_instance *svinst;

	/* If all capabilities are explicitly set, we don't need to
	   generate them. */
	if (config_parsed_get_setting_change_counter(new_config,
		"managesieve_login", "managesieve_sieve_capability") ==
			CONFIG_PARSER_CHANGE_EXPLICIT &&
	    config_parsed_get_setting_change_counter(new_config,
		"managesieve_login", "managesieve_notify_capability") ==
			CONFIG_PARSER_CHANGE_EXPLICIT &&
	    config_parsed_get_setting_change_counter(new_config,
		"managesieve_login", "managesieve_extlists_capability") ==
			CONFIG_PARSER_CHANGE_EXPLICIT)
		return 0;

	/* Initialize Sieve engine */
	struct sieve_environment svenv = {
		.home_dir = "/tmp",
		.event_parent = event,
	};
	if (sieve_init(&svenv, NULL, NULL, FALSE, &svinst) < 0) {
		*error_r = "Failed to initialize Sieve";
		return -1;
	}

	/* Dump capabilities */
	managesieve_login_config_set(ctx, "managesieve_sieve_capability",
				     sieve_get_capabilities(svinst, NULL));

	const char *capability_notify = sieve_get_capabilities(svinst, "notify");
	if (capability_notify != NULL) {
		managesieve_login_config_set(
			ctx, "managesieve_notify_capability",
			capability_notify);
	}
	const char *capability_extlists =
		sieve_get_capabilities(svinst, "extlists");
	if (capability_extlists != NULL) {
		managesieve_login_config_set(
			ctx, "managesieve_extlists_capability",
			capability_extlists);
	}

	sieve_deinit(&svinst);
	return 0;
}

static int
managesieve_login_config_parser_end(struct config_parser_context *ctx,
				    struct config_parsed *new_config,
				    struct event *event, const char **error_r)
{
	if (dump_capability(ctx, new_config, event, error_r) < 0)
		return -1;
	if (next_hook_config_parser_end != NULL)
		return next_hook_config_parser_end(ctx, new_config, event, error_r);
	return 0;
}
