noinst_LTLIBRARIES = libutil.la

AM_CPPFLAGS = \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
        -I$(dovecot_incdir)/src/lib-index \
        -I$(dovecot_incdir)/src/lib-storage \
        -I$(dovecot_incdir)/src/lib-storage/index \
        -I$(dovecot_incdir)/src/lib-storage/index/raw \
        -I$(dovecot_incdir)/src/lib-storage/index/mbox \
        -I$(dovecot_incdir)/src/lib-storage/index/maildir

libutil_la_SOURCES = \
	mail-raw.c

noinst_HEADERS = \
	mail-raw.h
