#ifndef EXT_VARIABLES_SETTINGS_H
#define EXT_VARIABLES_SETTINGS_H

struct ext_variables_settings {
	pool_t pool;

	/* Maximum number of variables (in a scope) */
	unsigned int max_scope_size;
	/* Maximum size of variable value */
	size_t max_variable_size;
};

extern const struct setting_parser_info ext_variables_setting_parser_info;

#endif
