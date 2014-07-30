noinst_LTLIBRARIES = libsieve_ext_mailbox.la

AM_CPPFLAGS = \
	-I$(srcdir)/../.. \
	$(LIBDOVECOT_INCLUDE)

tags = \
	tag-mailbox-create.c

tests = \
	tst-mailboxexists.c

libsieve_ext_mailbox_la_SOURCES = \
	$(tags) \
	$(tests) \
	ext-mailbox.c

public_headers = \
	sieve-ext-mailbox.h

headers = \
	ext-mailbox-common.h

pkginc_libdir=$(dovecot_pkgincludedir)/sieve
pkginc_lib_HEADERS = $(public_headers)
noinst_HEADERS = $(headers)
