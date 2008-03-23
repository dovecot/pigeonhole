noinst_LTLIBRARIES = libsieve_ext_relational.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

libsieve_ext_relational_la_SOURCES = \
	ext-relational-common.c \
	mcht-value.c \
	mcht-count.c \
	ext-relational.c 

noinst_HEADER = \
	ext-relational-common.h
