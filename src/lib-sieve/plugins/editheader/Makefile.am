noinst_LTLIBRARIES = libsieve_ext_editheader.la

AM_CPPFLAGS = \
	-I$(srcdir)/../.. \
	-I$(srcdir)/../../util \
	$(LIBDOVECOT_INCLUDE)

commands = \
	cmd-addheader.c \
	cmd-deleteheader.c

libsieve_ext_editheader_la_SOURCES = \
	$(commands) \
	ext-editheader.c \
	ext-editheader-settings.c \
	ext-editheader-common.c

noinst_HEADERS = \
	ext-editheader-limits.h \
	ext-editheader-settings.h \
	ext-editheader-common.h
