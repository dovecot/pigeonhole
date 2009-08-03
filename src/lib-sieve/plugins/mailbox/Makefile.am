noinst_LTLIBRARIES = libsieve_ext_mailbox.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

tags = \
	tag-mailbox-create.c

commands = \
	cmd-mailboxexists.c

libsieve_ext_mailbox_la_SOURCES = \
	$(tags) \
	$(commands) \
	ext-mailbox.c

noinst_HEADERS = \
	ext-mailbox-common.h
