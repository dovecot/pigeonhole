noinst_LTLIBRARIES = libsieve_ext_imap4flags.la

AM_CPPFLAGS = \
	-I../../ \
    -I../variables \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

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

noinst_HEADERS = \
	ext-imap4flags-common.h
