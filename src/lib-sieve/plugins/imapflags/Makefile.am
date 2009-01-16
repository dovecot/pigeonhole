noinst_LTLIBRARIES = libsieve_ext_imapflags.la

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

libsieve_ext_imapflags_la_SOURCES = \
	ext-imapflags-common.c \
	$(commands) \
	$(tests) \
	$(tags) \
	ext-imapflags.c

noinst_HEADERS = \
	ext-imapflags-common.h
