noinst_LTLIBRARIES = libsieve_ext_variables.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

cmds = \
	cmd-set.c

tsts = \
	tst-string.c

libsieve_ext_variables_la_SOURCES = \
	ext-variables-common.c \
	ext-variables-name.c \
	$(cmds) \
	$(tsts) \
	ext-variables.c

noinst_HEADERS = \
	ext-variables-common.h \
	ext-variables-name.h


