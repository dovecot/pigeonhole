noinst_LTLIBRARIES = libsieve_ext_mime.la

AM_CPPFLAGS = \
	-I$(srcdir)/../.. \
	-I$(srcdir)/../../util \
	-I$(srcdir)/../variables  \
	$(LIBDOVECOT_INCLUDE)

commands = \
	cmd-foreverypart.c \
	cmd-break.c \
	cmd-extracttext.c

tags = \
	tag-mime.c

extensions = \
	ext-mime.c \
	ext-foreverypart.c \
	ext-extracttext.c

libsieve_ext_mime_la_SOURCES = \
	ext-mime-common.c \
	$(commands) \
	$(tags) \
	$(extensions)

noinst_HEADERS = \
	ext-mime-common.h

