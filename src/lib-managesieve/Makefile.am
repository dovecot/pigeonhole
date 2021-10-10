noinst_LTLIBRARIES = libmanagesieve.la

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	-I$(top_srcdir)

libmanagesieve_la_SOURCES = \
	managesieve-arg.c \
	managesieve-quote.c \
	managesieve-parser.c \
	managesieve-url.c

noinst_HEADERS = \
	managesieve-protocol.h \
	managesieve-arg.h \
	managesieve-quote.h \
	managesieve-parser.h \
	managesieve-url.h

test_programs = \
	test-managesieve-url

test_nocheck_programs =

noinst_PROGRAMS = $(test_programs) $(test_nocheck_programs)

test_libs = \
	$(noinst_LTLIBRARIES) \
	$(LIBDOVECOT_STORAGE) \
	$(LIBDOVECOT)
test_deps = \
	$(noinst_LTLIBRARIES) \
	$(LIBDOVECOT_STORAGE_DEPS) \
	$(LIBDOVECOT_DEPS)

test_managesieve_url_SOURCES = test-managesieve-url.c
test_managesieve_url_LDADD = $(test_libs)
test_managesieve_url_DEPENDENCIES = $(test_deps)

check-local:
	for bin in $(test_programs); do \
	  if ! $(RUN_TEST) ./$$bin; then exit 1; fi; \
	done
