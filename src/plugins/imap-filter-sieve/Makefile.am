imap_moduledir = $(dovecot_moduledir)

imap_module_LTLIBRARIES = lib95_imap_filter_sieve_plugin.la

lib95_imap_filter_sieve_plugin_la_LDFLAGS = -module -avoid-version

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	$(LIBDOVECOT_IMAP_INCLUDE) \
	$(LIBDOVECOT_LDA_INCLUDE) \
	$(LIBDOVECOT_INCLUDE) \
	-DPKG_RUNDIR=\""$(rundir)"\"

lib95_imap_filter_sieve_plugin_la_SOURCES = \
	cmd-filter.c \
	cmd-filter-sieve.c \
	imap-filter.c \
	imap-filter-sieve.c \
	imap-filter-sieve-plugin.c
lib95_imap_filter_sieve_plugin_la_LIBADD = \
	$(top_builddir)/src/lib-sieve/libdovecot-sieve.la

noinst_HEADERS = \
	imap-filter.h \
	imap-filter-sieve.h \
	imap-filter-sieve-plugin.h
