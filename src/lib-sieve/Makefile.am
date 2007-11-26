SUBDIRS = plugins

noinst_LTLIBRARIES = libsieve.la

AM_CPPFLAGS = \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

tsts = \
	tst-not.c \
	tst-anyof.c \
	tst-allof.c \
	tst-address.c \
	tst-header.c \
	tst-exists.c \
	tst-size.c

cmds = \
	cmd-if.c \
	cmd-require.c \
	cmd-redirect.c \
	cmd-keep.c

exts = \
	ext-fileinto.c \
	ext-reject.c \
	ext-envelope.c

# These are not actual plugins just yet...
plugins = \
    ./plugins/vacation/libsieve_ext_vacation.la \
    ./plugins/subaddress/libsieve_ext_subaddress.la \
    ./plugins/comparator-i-ascii-numeric/libsieve_ext_comparator-i-ascii-numeric.la \
    ./plugins/relational/libsieve_ext_relational.la \
    ./plugins/regex/libsieve_ext_regex.la \
    ./plugins/imapflags/libsieve_ext_imapflags.la

libsieve_la_DEPENDENCIES = $(plugins)
libsieve_la_LIBADD = $(plugins)

libsieve_la_SOURCES = \
	sieve-lexer.c \
	sieve-ast.c \
	sieve-binary.c \
	sieve-parser.c \
	sieve-validator.c \
	sieve-generator.c \
	sieve-interpreter.c \
	sieve-result.c \
	sieve-error.c \
	sieve-comparators.c \
	sieve-match-types.c \
	sieve-address-parts.c \
	sieve-commands.c \
	sieve-code.c \
	$(tsts) \
	$(cmds) \
	sieve-extensions.c \
	$(exts) \
	sieve.c 

noinst_HEADERS = \
	sieve-lexer.h \
	sieve-ast.h \
	sieve-binary.h \
	sieve-parser.h \
	sieve-validator.h \
	sieve-generator.h \
	sieve-interpreter.h \
	sieve-result.h
	sieve-error.h \
	sieve-comparators.h \
	sieve-match-types.h \
	sieve-address-parts.h \
	sieve-commands.h \
	sieve-code.h \
	sieve-extensions.h \
	sieve.h
