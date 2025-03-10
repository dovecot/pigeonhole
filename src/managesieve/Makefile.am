settingsdir = $(dovecot_moduledir)/settings

dovecot_pkglibexec_PROGRAMS = managesieve

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_SERVICE_INCLUDE) \
	-DMODULEDIR=\""$(dovecot_moduledir)"\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-managesieve

libmanagesieve_settings_la_LDFLAGS = -module -avoid-version

settings_LTLIBRARIES = \
	libmanagesieve_settings.la

libmanagesieve_settings_la_SOURCES = \
	managesieve-settings.c

libs = \
	managesieve-settings.lo \
	$(top_builddir)/src/lib-managesieve/libdovecot-managesieve.la \
	$(top_builddir)/src/lib-sieve/libdovecot-sieve.la

managesieve_CPPFLAGS = $(AM_CPPFLAGS) $(BINARY_CFLAGS)
managesieve_LDFLAGS = -export-dynamic $(BINARY_LDFLAGS)

managesieve_LDADD = $(libs) $(LIBDOVECOT_STORAGE) $(LIBDOVECOT_LDA) $(LIBDOVECOT)

managesieve_DEPENDENCIES = $(libs) $(LIBDOVECOT_STORAGE_DEPS) $(LIBDOVECOT_LDA_DEPS) $(LIBDOVECOT_DEPS)

cmds = \
	cmd-capability.c \
	cmd-logout.c \
	cmd-putscript.c \
	cmd-getscript.c \
	cmd-setactive.c \
	cmd-deletescript.c \
	cmd-listscripts.c \
	cmd-havespace.c \
	cmd-renamescript.c \
	cmd-noop.c

managesieve_SOURCES = \
	$(cmds) \
	managesieve-quota.c \
	managesieve-client.c \
	managesieve-commands.c \
	main.c

noinst_HEADERS = \
	managesieve-quota.h \
	managesieve-client.h \
	managesieve-commands.h \
	managesieve-settings.h \
	managesieve-common.h
