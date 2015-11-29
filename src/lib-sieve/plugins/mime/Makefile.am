noinst_LTLIBRARIES = libsieve_ext_mime.la

AM_CPPFLAGS = \
	-I$(srcdir)/../.. \
	-I$(srcdir)/../../util \
	$(LIBDOVECOT_INCLUDE)

commands = \
	cmd-foreverypart.c \
	cmd-break.c

tags = \
	tag-mime.c

libsieve_ext_mime_la_SOURCES = \
	ext-mime.c \
	ext-foreverypart.c \
	ext-mime-common.c \
	$(commands) \
	$(tags)

noinst_HEADERS = \
	ext-mime-common.h

