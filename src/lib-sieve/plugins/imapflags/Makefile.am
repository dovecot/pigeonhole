noinst_LTLIBRARIES = libsieve_ext_imapflags.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

cmds = \
	cmd-setflag.c \
	cmd-addflag.c \
	cmd-removeflag.c

libsieve_ext_imapflags_la_SOURCES = \
	ext-imapflags-common.c \
	$(cmds) \
	ext-imapflags.c

noinst_HEADERS = \
	ext-imapflags-common.h
