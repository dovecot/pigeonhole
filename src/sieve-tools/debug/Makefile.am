noinst_LTLIBRARIES = libsieve_ext_debug.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

commands = \
	cmd-debug-print.c

libsieve_ext_debug_la_SOURCES = \
	$(commands) \
	ext-debug.c

noinst_HEADERS = \
	sieve-ext-debug.h \
	ext-debug-common.h
