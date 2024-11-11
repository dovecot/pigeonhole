/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"
#include "array.h"
#include "sort.h"
#include "settings.h"
#include "settings-parser.h"

#include "sieve-script.h"
#include "sieve-storage.h"
#include "sieve-storage-settings.h"

static bool
sieve_storage_settings_check(void *_set, pool_t pool, const char **error_r);

#undef DEF
#define DEF(type, name) SETTING_DEFINE_STRUCT_##type( \
	"sieve_"#name, name, \
	struct sieve_storage_settings)

static const struct setting_filter_array_order sieve_storage_order_precedence = {
	.info = &sieve_storage_setting_parser_info,
	.field_name = "sieve_script_precedence",
};

static const struct setting_define sieve_storage_setting_defines[] = {
	DEF(STR, script_storage),
	DEF(UINT, script_precedence),

	DEF(STR, script_type),
	DEF(BOOLLIST, script_cause),
	DEF(STR, script_driver),
	DEF(STR, script_name),
	DEF(STR, script_bin_path),

	DEF(SIZE, quota_storage_size),
	DEF(UINT, quota_script_count),

	{ .type = SET_FILTER_ARRAY, .key = "sieve_script",
	   .offset = offsetof(struct sieve_storage_settings, storages),
	   .filter_array_field_name = "sieve_script_storage",
	   .filter_array_order = &sieve_storage_order_precedence },

	SETTING_DEFINE_LIST_END,
};

static const struct sieve_storage_settings sieve_storage_default_settings = {
	.script_storage = "",
	.script_precedence = UINT_MAX,

	.script_type = SIEVE_STORAGE_TYPE_PERSONAL,
	.script_cause = ARRAY_INIT,

	.script_driver = "",
	.script_name = "",
	.script_bin_path = "",

	.quota_storage_size = 0,
	.quota_script_count = 0,

	.storages = ARRAY_INIT,
};

const struct setting_parser_info sieve_storage_setting_parser_info = {
	.name = "sieve_storage",

	.defines = sieve_storage_setting_defines,
	.defaults = &sieve_storage_default_settings,

	.struct_size = sizeof(struct sieve_storage_settings),

	.pool_offset1 = 1 + offsetof(struct sieve_storage_settings, pool),

	.check_func = sieve_storage_settings_check,
};

/* <settings checks> */
static bool
sieve_storage_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			     const char **error_r)
{
	struct sieve_storage_settings *set = _set;

	if (*set->script_storage != '\0' &&
	    !sieve_storage_name_is_valid(set->script_storage)) {
		*error_r = t_strdup_printf(
			"Invalid script storage name '%s'",
			str_sanitize(set->script_storage, 128));
		return FALSE;
	}
	if (*set->script_name != '\0' &&
	    !sieve_script_name_is_valid(set->script_name)) {
		*error_r = t_strdup_printf(
			"Invalid script name '%s'",
			str_sanitize(set->script_name, 128));
		return FALSE;
	}

	if (array_is_created(&set->script_cause))
		array_sort(&set->script_cause, i_strcmp_p);

	return TRUE;
}
/* </settings checks> */

bool sieve_storage_settings_match_script_type(
	const struct sieve_storage_settings *set, const char *type)
{
	if (strcasecmp(type, SIEVE_STORAGE_TYPE_ANY) == 0)
		return TRUE;
	if (strcasecmp(type, set->script_type) == 0)
		return TRUE;
	return FALSE;
}

bool sieve_storage_settings_match_script_cause(
	const struct sieve_storage_settings *set, const char *cause)
{
	if (strcasecmp(cause, SIEVE_SCRIPT_CAUSE_ANY) == 0) {
		/* Any cause will match */
		return TRUE;
	}
	if (!array_is_created(&set->script_cause)) {
		/* Causes are not configured for this storage */
		if (strcasecmp(set->script_type,
			       SIEVE_STORAGE_TYPE_PERSONAL) == 0) {
			/* For personal storages the default is to match any
			   cause. */
			return TRUE;
		}
		if (strcasecmp(cause, SIEVE_SCRIPT_CAUSE_DELIVERY) == 0) {
			/* The default cause is delivery */
			return TRUE;
		}
		return FALSE;
	}

	/* Causes are configured for this storage: perform lookup */

	unsigned int set_cause_count;
	const char *const *set_cause;

	set_cause = array_get(&set->script_cause, &set_cause_count);
	return (i_bsearch(cause, set_cause, set_cause_count,
			 sizeof(const char *), search_strcasecmp) != NULL);
}
