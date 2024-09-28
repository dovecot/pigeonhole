imap_moduledir = $(dovecot_moduledir)
sieve_plugindir = $(dovecot_moduledir)/sieve

imap_module_LTLIBRARIES = lib95_imap_sieve_plugin.la
sieve_plugin_LTLIBRARIES = lib90_sieve_imapsieve_plugin.la

lib95_imap_sieve_plugin_la_LDFLAGS = -module -avoid-version
lib90_sieve_imapsieve_plugin_la_LDFLAGS = -module -avoid-version

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sieve/util \
	-I$(top_srcdir)/src/lib-sieve/plugins/environment \
	$(LIBDOVECOT_IMAP_INCLUDE) \
	$(LIBDOVECOT_LDA_INCLUDE) \
	$(LIBDOVECOT_INCLUDE) \
	-DPKG_RUNDIR=\""$(rundir)"\"

lib95_imap_sieve_plugin_la_SOURCES = \
	ext-imapsieve.c \
	ext-imapsieve-environment.c \
	imap-sieve.c \
	imap-sieve-settings.c \
	imap-sieve-storage.c \
	imap-sieve-plugin.c
lib95_imap_sieve_plugin_la_LIBADD = \
	$(top_builddir)/src/lib-sieve/libdovecot-sieve.la

lib90_sieve_imapsieve_plugin_la_SOURCES = \
	ext-imapsieve.c \
	sieve-imapsieve-plugin.c
lib90_sieve_imapsieve_plugin_la_CPPFLAGS = \
	${AM_CPPFLAGS} \
	-D__IMAPSIEVE_DUMMY

noinst_HEADERS = \
	ext-imapsieve-common.h \
	imap-sieve.h \
	imap-sieve-settings.h \
	imap-sieve-storage.h \
	imap-sieve-plugin.h \
	sieve-imapsieve-plugin.h
