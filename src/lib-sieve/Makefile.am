SUBDIRS = plugins

noinst_LTLIBRARIES = libsieve.la

AM_CPPFLAGS = \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

tests = \
	tst-not.c \
	tst-anyof.c \
	tst-allof.c \
	tst-address.c \
	tst-header.c \
	tst-exists.c \
	tst-size.c

commands = \
	cmd-if.c \
	cmd-require.c \
	cmd-redirect.c \
	cmd-keep.c \
	cmd-discard.c

extensions = \
	ext-fileinto.c \
	ext-reject.c \
	ext-envelope.c \
	ext-encoded-character.c

match_types = \
	mcht-is.c \
	mcht-contains.c \
	mcht-matches.c

comparators = \
	cmp-i-octet.c \
	cmp-i-ascii-casemap.c

# These are not actual plugins just yet...
plugins = \
    ./plugins/vacation/libsieve_ext_vacation.la \
    ./plugins/subaddress/libsieve_ext_subaddress.la \
    ./plugins/comparator-i-ascii-numeric/libsieve_ext_comparator-i-ascii-numeric.la \
    ./plugins/relational/libsieve_ext_relational.la \
    ./plugins/regex/libsieve_ext_regex.la \
    ./plugins/copy/libsieve_ext_copy.la \
    ./plugins/imapflags/libsieve_ext_imapflags.la \
    ./plugins/include/libsieve_ext_include.la \
    ./plugins/body/libsieve_ext_body.la \
    ./plugins/variables/libsieve_ext_variables.la

libsieve_la_DEPENDENCIES = $(plugins)
libsieve_la_LIBADD = $(plugins)

libsieve_la_SOURCES = \
	sieve-message.c \
	sieve-lexer.c \
	sieve-script.c \
	sieve-ast.c \
	sieve-binary.c \
	sieve-parser.c \
	sieve-address.c \
	sieve-validator.c \
	sieve-generator.c \
	sieve-interpreter.c \
	sieve-code-dumper.c \
	sieve-binary-dumper.c \
	sieve-result.c \
	sieve-error.c \
	sieve-objects.c \
	sieve-comparators.c \
	sieve-match-types.c \
	sieve-address-parts.c \
	sieve-match.c \
	sieve-commands.c \
	sieve-code.c \
	sieve-actions.c \
	sieve-extensions.c \
	$(comparators) \
	$(match_types) \
	$(tests) \
	$(commands) \
	$(extensions) \
	sieve.c 

noinst_HEADERS = \
	sieve-types.h \
	sieve-common.h \
	sieve-message.h \
	sieve-lexer.h \
	sieve-script.h \
	sieve-script-private.h \
	sieve-ast.h \
	sieve-binary.h \
	sieve-parser.h \
	sieve-address.h \
	sieve-validator.h \
	sieve-generator.h \
	sieve-interpreter.h \
	sieve-code-dumper.h \
	sieve-binary-dumper.h \
	sieve-dump.h \
	sieve-result.h \
	sieve-error.h \
	sieve-error-private.h \
	sieve-match.h \
	sieve-comparators.h \
	sieve-match-types.h \
	sieve-address-parts.h \
	sieve-commands.h \
	sieve-commands-private.h \
	sieve-code.h \
	sieve-actions.h \
	sieve-extensions.h \
	sieve.h
