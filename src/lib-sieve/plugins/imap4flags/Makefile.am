noinst_LTLIBRARIES = libsieve_ext_imap4flags.la

AM_CPPFLAGS = \
	-I$(srcdir)/../.. \
	-I$(srcdir)/../variables  \
	$(LIBDOVECOT_INCLUDE)

commands = \
	cmd-flag.c

tests = \
	tst-hasflag.c

tags = \
	tag-flags.c

libsieve_ext_imap4flags_la_SOURCES = \
	ext-imap4flags-common.c \
	$(commands) \
	$(tests) \
	$(tags) \
	ext-imap4flags.c

public_headers = \
	sieve-ext-imap4flags.h

headers = \
	ext-imap4flags-common.h

pkginc_libdir=$(dovecot_pkgincludedir)/sieve
pkginc_lib_HEADERS = $(public_headers)
noinst_HEADERS = $(headers)
