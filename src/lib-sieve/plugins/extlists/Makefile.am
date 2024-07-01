noinst_LTLIBRARIES = libsieve_ext_extlists.la

AM_CPPFLAGS = \
	-I$(srcdir)/../.. \
	-I$(srcdir)/../../util \
	$(LIBDOVECOT_INCLUDE)

tags = \
	tag-redirect-list.c

match_types = \
	mcht-list.c

tests = \
	tst-valid-ext-list.c

libsieve_ext_extlists_la_SOURCES = \
	$(tags) \
	$(match_types) \
	$(tests) \
	ext-extlists-settings.c \
	ext-extlists-common.c \
	ext-extlists.c

noinst_HEADERS = \
	ext-extlists-settings.h \
	ext-extlists-common.h

