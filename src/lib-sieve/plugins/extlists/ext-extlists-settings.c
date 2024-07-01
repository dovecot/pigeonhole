/* Copyright (c) 2025 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "uri-util.h"
#include "settings.h"
#include "settings-parser.h"

#include "urn.h"

#include "ext-extlists-common.h"
#include "ext-extlists-settings.h"

static bool
ext_extlists_list_settings_check(void *_set, pool_t pool,
				 const char **error_r);

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_extlists_list_"#name, name, \
				     struct ext_extlists_list_settings)

static const struct setting_define ext_extlists_list_setting_defines[] = {
	DEF(STR, name),
	DEF(SIZE, max_lookup_size),

	SETTING_DEFINE_LIST_END,
};

static const struct ext_extlists_list_settings ext_extlists_list_default_settings = {
	.name = "",
	.max_lookup_size = 1024,
};

const struct setting_parser_info ext_extlists_list_setting_parser_info = {
	.name = "sieve_extlists_list",

	.defines = ext_extlists_list_setting_defines,
	.defaults = &ext_extlists_list_default_settings,

	.struct_size = sizeof(struct ext_extlists_list_settings),

	.check_func = ext_extlists_list_settings_check,

	.pool_offset1 = 1 + offsetof(struct ext_extlists_list_settings, pool),
};

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_extlists_"#name, name, \
				     struct ext_extlists_settings)

static const struct setting_define ext_extlists_setting_defines[] = {
	{ .type = SET_FILTER_ARRAY,
	  .key = "sieve_extlists_list",
	  .filter_array_field_name = "sieve_extlists_list_name",
	  .offset = offsetof(struct ext_extlists_settings, lists), },

	SETTING_DEFINE_LIST_END,
};

static const struct ext_extlists_settings ext_extlists_default_settings = {
	.lists = ARRAY_INIT,
};

const struct setting_parser_info ext_extlists_setting_parser_info = {
	.name = "sieve_extlists",

	.defines = ext_extlists_setting_defines,
	.defaults = &ext_extlists_default_settings,

	.struct_size = sizeof(struct ext_extlists_settings),

	.pool_offset1 = 1 + offsetof(struct ext_extlists_settings, pool),
};

/* <settings checks> */
int ext_extlists_name_normalize(const char **name, const char **error_r)
{
	const char *uri = *name, *scheme;
	const char *error;

	if (*uri == ':')
		uri = t_strconcat(SIEVE_URN_PREFIX, uri, NULL);
	if (uri_cut_scheme(&uri, &scheme) < 0) {
		*error_r = "Invalid URI scheme";
		return -1;
	}
	scheme = t_str_lcase(scheme);
	if (strcmp(scheme, "urn") == 0) {
		if (urn_normalize(uri, URN_PARSE_SCHEME_EXTERNAL,
				  &uri, &error) < 0) {
			*error_r = t_strconcat("Invalid URN: ", error, NULL);
			return -1;
		}
		*name = t_strconcat(scheme, ":", uri, NULL);
		return 1;
	} else if (strcmp(scheme, "tag") == 0) {
		if (uri_check(uri, URI_PARSE_SCHEME_EXTERNAL, error_r) < 0) {
			*error_r = t_strconcat("Invalid TAG URI: ",
					       error, NULL);
			return -1;
		}
		*name = t_strconcat(scheme, ":", uri, NULL);
		return 1;
	}
	*error_r = t_strconcat(
		scheme, ": scheme not supported for external list name", NULL);
	return -1;
}

static bool
ext_extlists_list_settings_check(void *_set, pool_t pool, const char **error_r)
{
	struct ext_extlists_list_settings *set = _set;
	const char *norm_name;
	const char *error;

	if (*set->name != '\0') {
		norm_name = set->name;
		if (ext_extlists_name_normalize(&norm_name, &error) < 0) {
			*error_r = t_strdup_printf("List name '%s' is invalid: %s",
						   set->name, error);
			return FALSE;
		}
		set->parsed.name = p_strdup(pool, norm_name);
	}

	return TRUE;
}
/* </settings checks> */

