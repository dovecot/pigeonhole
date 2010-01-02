noinst_LTLIBRARIES = libsieve_ext_include.la

AM_CPPFLAGS = \
	-I../../ \
	-I../variables \
	$(SIEVE_LIB_DOVECOT_INCLUDE)

cmds = \
	cmd-include.c \
	cmd-return.c \
	cmd-global.c

libsieve_ext_include_la_SOURCES = \
	$(cmds) \
	ext-include-common.c \
	ext-include-binary.c \
	ext-include-variables.c \
	ext-include.c

noinst_HEADERS = \
	ext-include-common.h \
	ext-include-limits.h \
	ext-include-binary.h \
	ext-include-variables.h
