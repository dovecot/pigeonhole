bin_PROGRAMS = sievec sieve-dump sieve-test sieve-filter

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sieve-tool \
	-I$(srcdir)/debug \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_SERVICE_INCLUDE)

libs = \
	$(top_builddir)/src/lib-sieve/libdovecot-sieve.la \
	$(top_builddir)/src/lib-sieve-tool/libsieve-tool.la

libs_ldadd = $(libs) $(LIBDOVECOT_STORAGE) $(LIBDOVECOT_LDA) $(LIBDOVECOT)
libs_deps = $(libs) $(LIBDOVECOT_STORAGE_DEPS) $(LIBDOVECOT_LDA_DEPS) $(LIBDOVECOT_DEPS)

# Sieve Compile Tool

sievec_CPPFLAGS = $(AM_CPPFLAGS) $(BINARY_CFLAGS)
sievec_LDFLAGS = -export-dynamic $(BINARY_LDFLAGS)
sievec_LDADD = $(libs_ldadd)
sievec_DEPENDENCIES = $(libs_deps)

sievec_SOURCES = \
	sievec.c

# Sieve Dump Tool

sieve_dump_CPPFLAGS = $(AM_CPPFLAGS) $(BINARY_CFLAGS)
sieve_dump_LDFLAGS = -export-dynamic $(BINARY_LDFLAGS)
sieve_dump_LDADD = $(libs_ldadd)
sieve_dump_DEPENDENCIES = $(libs_deps)

sieve_dump_SOURCES = \
	sieve-dump.c

# Sieve Test Tool

sieve_test_CPPFLAGS = $(AM_CPPFLAGS) $(BINARY_CFLAGS)
sieve_test_LDFLAGS = -export-dynamic $(BINARY_LDFLAGS)
sieve_test_LDADD = $(libs_ldadd)
sieve_test_DEPENDENCIES = $(libs_deps)

sieve_test_SOURCES = \
	sieve-test.c

## Unfinished tools

# Sieve Filter Tool

sieve_filter_CPPFLAGS = $(AM_CPPFLAGS) $(BINARY_CFLAGS)
sieve_filter_LDFLAGS = -export-dynamic $(BINARY_LDFLAGS)
sieve_filter_LDADD = $(libs_ldadd)
sieve_filter_DEPENDENCIES = $(libs_deps)

sieve_filter_SOURCES = \
	sieve-filter.c

noinst_HEADERS =
