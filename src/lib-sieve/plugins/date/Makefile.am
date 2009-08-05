noinst_LTLIBRARIES = libsieve_ext_date.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

tests = \
	tst-date.c

libsieve_ext_date_la_SOURCES = \
	$(tests) \
	ext-date.c

noinst_HEADERS = \
	ext-date-common.h
