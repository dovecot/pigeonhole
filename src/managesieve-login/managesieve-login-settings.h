/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __MANAGESIEVE_LOGIN_SETTINGS_H
#define __MANAGESIEVE_LOGIN_SETTINGS_H

struct managesieve_login_settings {
	const char *managesieve_implementation_string;
	const char *managesieve_sieve_capability;
};

extern const struct setting_parser_info *managesieve_login_settings_set_roots[];

#endif /* __MANAGESIEVE_LOGIN_SETTINGS_H */
