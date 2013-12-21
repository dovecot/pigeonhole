noinst_LTLIBRARIES = libsieve_ext_debug.la

AM_CPPFLAGS = \
	-I$(srcdir)/../../.. \
	$(LIBDOVECOT_INCLUDE)

commands = \
	cmd-debug-log.c

libsieve_ext_debug_la_SOURCES = \
	$(commands) \
	ext-debug.c

noinst_HEADERS = \
	ext-debug-common.h
