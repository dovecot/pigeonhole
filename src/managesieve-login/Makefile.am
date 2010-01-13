settingsdir = $(moduledir)/settings

dovecot_pkglibexec_PROGRAMS = managesieve-login

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_SERVICE_INCLUDE) \
	$(LIBDOVECOT_LOGIN_INCLUDE) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/lib-managesieve

libmanagesieve_login_settings_la_LDFLAGS = -module -avoid-version

settings_LTLIBRARIES = \
	libmanagesieve_login_settings.la

libmanagesieve_login_settings_la_SOURCES = \
	managesieve-login-settings.c

managesieve_login_LDADD = \
	managesieve-login-settings.lo \
	$(top_srcdir)/src/lib-managesieve/libmanagesieve.a \
	$(LIBDOVECOT_LOGIN) \
	$(LIBDOVECOT)

managesieve_login_SOURCES = \
	client.c \
	client-authenticate.c \
	managesieve-proxy.c

noinst_HEADERS = \
	client.h \
	client-authenticate.h \
	managesieve-login-settings.h \
	managesieve-proxy.h
