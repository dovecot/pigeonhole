/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "sieve-extprograms-limits.h"
#include "sieve-extprograms-settings.h"

static bool
sieve_extprograms_settings_check(void *_set, pool_t pool, const char **error_r);

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_pipe_"#name, name, \
				     struct sieve_extprograms_settings)

static const struct setting_define ext_pipe_setting_defines[] = {
	DEF(STR, bin_dir),
	DEF(STR, socket_dir),
	DEF(ENUM, input_eol),

	DEF(TIME, exec_timeout),

	SETTING_DEFINE_LIST_END,
};

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_filter_"#name, name, \
				     struct sieve_extprograms_settings)

static const struct setting_define ext_filter_setting_defines[] = {
	DEF(STR, bin_dir),
	DEF(STR, socket_dir),
	DEF(ENUM, input_eol),

	DEF(TIME, exec_timeout),

	SETTING_DEFINE_LIST_END,
};

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_execute_"#name, name, \
				     struct sieve_extprograms_settings)

static const struct setting_define ext_execute_setting_defines[] = {
	DEF(STR, bin_dir),
	DEF(STR, socket_dir),
	DEF(ENUM, input_eol),

	DEF(TIME, exec_timeout),

	SETTING_DEFINE_LIST_END,
};

static const struct sieve_extprograms_settings sieve_extprograms_default_settings = {
	.bin_dir = "",
	.socket_dir = "",
	.input_eol = "crlf:lf",
	.exec_timeout = 10,
};

const struct setting_parser_info sieve_ext_vnd_pipe_setting_parser_info = {
	.name = "sieve_ext_pipe",

	.defines = ext_pipe_setting_defines,
	.defaults = &sieve_extprograms_default_settings,

	.struct_size = sizeof(struct sieve_extprograms_settings),

	.check_func = sieve_extprograms_settings_check,

	.pool_offset1 = 1 + offsetof(struct sieve_extprograms_settings, pool),
};

const struct setting_parser_info sieve_ext_vnd_filter_setting_parser_info = {
	.name = "sieve_ext_filter",

	.defines = ext_filter_setting_defines,
	.defaults = &sieve_extprograms_default_settings,

	.struct_size = sizeof(struct sieve_extprograms_settings),

	.check_func = sieve_extprograms_settings_check,

	.pool_offset1 = 1 + offsetof(struct sieve_extprograms_settings, pool),
};

const struct setting_parser_info sieve_ext_vnd_execute_setting_parser_info = {
	.name = "sieve_ext_execute",

	.defines = ext_execute_setting_defines,
	.defaults = &sieve_extprograms_default_settings,

	.struct_size = sizeof(struct sieve_extprograms_settings),

	.check_func = sieve_extprograms_settings_check,

	.pool_offset1 = 1 + offsetof(struct sieve_extprograms_settings, pool),
};

/* <settings checks> */
static bool
sieve_extprograms_settings_check(void *_set, pool_t pool ATTR_UNUSED,
				 const char **error_r ATTR_UNUSED)
{
	struct sieve_extprograms_settings *set = _set;

	if (strcasecmp(set->input_eol, "crlf") == 0)
		set->parsed.input_eol = SIEVE_EXTPROGRAMS_EOL_CRLF;
	else if (strcasecmp(set->input_eol, "lf") == 0)
		set->parsed.input_eol = SIEVE_EXTPROGRAMS_EOL_LF;
	else
		i_unreached();
	return TRUE;
}
/* </settings checks> */
