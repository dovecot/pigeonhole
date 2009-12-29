noinst_LTLIBRARIES = libsieve_ext_spamvirustest.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

tests = \
	tst-spamvirustest.c

libsieve_ext_spamvirustest_la_SOURCES = \
	$(tests) \
	ext-spamvirustest-common.c \
	ext-spamvirustest.c

noinst_HEADERS = \
	ext-spamvirustest-common.h
