noinst_LTLIBRARIES = libsieve_util.la

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_SERVICE_INCLUDE) \
	-DMODULEDIR=\""$(dovecot_moduledir)"\"

libsieve_util_la_DEPENDENCIES = $(LIBDOVECOT_STORAGE_DEPS) $(LIBDOVECOT_DEPS)

libsieve_util_la_SOURCES = \
	mail-raw.c \
	edit-mail.c \
	rfc2822.c \
	realpath.c \
	strtrim.c

headers = \
	mail-raw.h \
	edit-mail.h \
	rfc2822.h \
	realpath.h \
	strtrim.h

pkginc_libdir=$(dovecot_pkgincludedir)/sieve
pkginc_lib_HEADERS = $(headers)

test_programs = \
	test-realpath \
	test-edit-mail

noinst_PROGRAMS = $(test_programs)

test_libs = \
	libsieve_util.la \
	$(LIBDOVECOT_STORAGE) \
	$(LIBDOVECOT)
test_deps = \
	libsieve_util.la \
	$(LIBDOVECOT_STORAGE_DEPS) \
	$(LIBDOVECOT_DEPS)

test_realpath_SOURCES = test-realpath.c
test_realpath_LDADD = $(test_libs)
test_realpath_DEPENDENCIES = $(test_deps)

test_edit_mail_SOURCES = test-edit-mail.c
test_edit_mail_LDADD = $(test_libs)
test_edit_mail_DEPENDENCIES = $(test_deps)

check: check-am check-test
check-test: all-am
	for bin in $(test_programs); do \
	  if ! $(RUN_TEST) ./$$bin; then exit 1; fi; \
	done
