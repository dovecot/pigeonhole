noinst_LTLIBRARIES = libsieve_ext_vacation.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

cmds = \
	cmd-vacation.c

libsieve_ext_vacation_la_SOURCES = \
	$(cmds) \
	ext-vacation.c

noinst_HEADERS = \
	ext-vacation-common.h

EXTRA_DIST = *.txt *.sieve
