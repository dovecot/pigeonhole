#noinst_LTLIBRARIES = libsieve-dovecot.la

SUBDIRS = plugins

sbin_PROGRAMS = sievec

AM_CPPFLAGS = \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

sievec_LDADD = \
	${dovecot_incdir}/src/lib/liblib.a \
	./plugins/vacation/lib_ext_vacation.a

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
	cmd-redirect.c 	

exts = \
	ext-fileinto.c \
	ext-reject.c \
	ext-envelope.c

sievec_SOURCES = \
	sieve-lexer.c \
	sieve-ast.c \
	sieve-binary.c \
	sieve-parser.c \
	sieve-validator.c \
	sieve-generator.c \
	sieve-interpreter.c \
	sieve-result.c \
	sieve-error.c \
	sieve-commands.c \
	sieve-code.c \
	$(tsts) \
	$(cmds) \
	sieve-extensions.c \
	$(exts) \
	sievec.c 

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
	sieve-commands.h \
	sieve-code.h
	sieve-extensions.h
