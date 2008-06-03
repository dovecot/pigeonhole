noinst_LTLIBRARIES = libsieve_ext_body.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

tsts = \
	tst-body.c

libsieve_ext_body_la_SOURCES = \
	ext-body-common.c \
	$(tsts) \
	ext-body.c

noinst_HEADERS = \
	ext-body-common.h

EXTRA_DIST = *.txt *.sieve
