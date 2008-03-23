noinst_LTLIBRARIES = libsieve_ext_regex.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

libsieve_ext_regex_la_SOURCES = \
	mcht-regex.c \
	ext-regex.c

noinst_HEADERS = \
	ext-regex-common.h
