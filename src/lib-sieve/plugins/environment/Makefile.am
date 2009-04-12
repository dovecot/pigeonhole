noinst_LTLIBRARIES = libsieve_ext_environment.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

tests = \
	tst-environment.c

libsieve_ext_environment_la_SOURCES = \
	$(tests) \
	ext-environment-common.c \
	ext-environment.c

noinst_HEADERS = \
	ext-environment-common.h \
	sieve-ext-environment.h
