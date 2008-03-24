noinst_LTLIBRARIES = libsieve_ext_include.la

AM_CPPFLAGS = \
	-I../../ \
	-I../variables \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage

cmds = \
	cmd-include.c \
	cmd-return.c \
	cmd-import.c \
	cmd-export.c

libsieve_ext_include_la_SOURCES = \
	$(cmds) \
	ext-include-common.c \
	ext-include-binary.c \
	ext-include.c

noinst_HEADERS = \
	ext-include-common.h \
	ext-include-binary.h
