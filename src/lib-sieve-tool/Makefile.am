noinst_LTLIBRARIES = libsieve-tool.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-master \
	-I$(dovecot_incdir)/src/lib-settings \
	-I$(dovecot_incdir)/src/lib-index \
	-I$(dovecot_incdir)/src/lib-storage \
	-I$(dovecot_incdir)/src/lib-storage/index \
	-I$(dovecot_incdir)/src/lib-storage/index/raw

libsieve_tool_la_SOURCES = \
	sieve-tool.c \
	mail-raw.c

noinst_HEADERS = \
	sieve-tool.h \
	mail-raw.h
