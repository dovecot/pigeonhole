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
	urn.c

headers = \
	mail-raw.h \
	edit-mail.h \
	rfc2822.h \
	urn.h

pkginc_libdir=$(dovecot_pkgincludedir)/sieve
pkginc_lib_HEADERS = $(headers)

test_programs = \
	test-edit-mail \
	test-rfc2822 \
	test-urn

noinst_PROGRAMS = $(test_programs)

test_libs = \
	libsieve_util.la \
	$(LIBDOVECOT_STORAGE) \
	$(LIBDOVECOT)
test_deps = \
	libsieve_util.la \
	$(LIBDOVECOT_STORAGE_DEPS) \
	$(LIBDOVECOT_DEPS)

test_edit_mail_SOURCES = test-edit-mail.c
test_edit_mail_LDADD = $(test_libs)
test_edit_mail_DEPENDENCIES = $(test_deps)

test_rfc2822_SOURCES = test-rfc2822.c
test_rfc2822_LDADD = $(test_libs)
test_rfc2822_DEPENDENCIES = $(test_deps)

test_urn_SOURCES = test-urn.c
test_urn_LDADD = $(test_libs)
test_urn_DEPENDENCIES = $(test_deps)

check: check-am check-test
check-test: all-am
	for bin in $(test_programs); do \
	  if ! $(RUN_TEST) ./$$bin; then exit 1; fi; \
	done

