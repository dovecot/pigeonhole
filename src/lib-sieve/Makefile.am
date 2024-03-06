SUBDIRS = util storage plugins

dovecot_pkglib_LTLIBRARIES = libdovecot-sieve.la

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_SERVICE_INCLUDE) \
	-I$(top_srcdir)/src/lib-sieve/util \
	-DMODULEDIR=\""$(dovecot_moduledir)"\"

tests = \
	tst-truefalse.c \
	tst-not.c \
	tst-anyof.c \
	tst-allof.c \
	tst-address.c \
	tst-header.c \
	tst-exists.c \
	tst-size.c

commands = \
	cmd-require.c \
	cmd-stop.c \
	cmd-if.c \
	cmd-keep.c \
	cmd-redirect.c \
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

if BUILD_UNFINISHED
unfinished_storages =
unfinished_plugins =
endif

strgdir = $(top_builddir)/src/lib-sieve/storage
storages = \
	$(strgdir)/data/libsieve_storage_data.la \
	$(strgdir)/file/libsieve_storage_file.la \
	$(strgdir)/dict/libsieve_storage_dict.la \
	$(strgdir)/ldap/libsieve_storage_ldap.la \
	$(unfinished_storages)

extdir = $(top_builddir)/src/lib-sieve/plugins
plugins = \
	$(extdir)/vacation/libsieve_ext_vacation.la \
	$(extdir)/subaddress/libsieve_ext_subaddress.la \
 	$(extdir)/comparator-i-ascii-numeric/libsieve_ext_comparator-i-ascii-numeric.la \
	$(extdir)/relational/libsieve_ext_relational.la \
	$(extdir)/regex/libsieve_ext_regex.la \
	$(extdir)/copy/libsieve_ext_copy.la \
	$(extdir)/imap4flags/libsieve_ext_imap4flags.la \
	$(extdir)/include/libsieve_ext_include.la \
	$(extdir)/body/libsieve_ext_body.la \
	$(extdir)/variables/libsieve_ext_variables.la \
	$(extdir)/enotify/libsieve_ext_enotify.la \
	$(extdir)/environment/libsieve_ext_environment.la \
	$(extdir)/mailbox/libsieve_ext_mailbox.la \
	$(extdir)/date/libsieve_ext_date.la \
	$(extdir)/spamvirustest/libsieve_ext_spamvirustest.la \
	$(extdir)/ihave/libsieve_ext_ihave.la \
	$(extdir)/editheader/libsieve_ext_editheader.la \
	$(extdir)/duplicate/libsieve_ext_duplicate.la \
	$(extdir)/index/libsieve_ext_index.la \
	$(extdir)/metadata/libsieve_ext_metadata.la \
	$(extdir)/mime/libsieve_ext_mime.la \
	$(extdir)/special-use/libsieve_ext_special_use.la \
	$(extdir)/vnd.dovecot/debug/libsieve_ext_debug.la \
	$(extdir)/vnd.dovecot/environment/libsieve_ext_vnd_environment.la \
	$(extdir)/vnd.dovecot/report/libsieve_ext_vnd_report.la \
	$(unfinished_plugins)

libdovecot_sieve_la_DEPENDENCIES = \
	$(storages) \
	$(plugins) \
	$(top_builddir)/src/lib-sieve/util/libsieve_util.la \
	$(LIBDOVECOT_STORAGE_DEPS) \
	$(LIBDOVECOT_DEPS)
libdovecot_sieve_la_LIBADD = \
	$(storages) \
	$(plugins) \
	$(top_builddir)/src/lib-sieve/util/libsieve_util.la \
	$(LIBDOVECOT_STORAGE) \
	$(LIBDOVECOT)

libdovecot_sieve_la_SOURCES = \
	sieve-settings.old.c \
	sieve-settings.c \
	sieve-message.c \
	sieve-smtp.c \
	sieve-lexer.c \
	sieve-script.c \
	sieve-storage.c \
	sieve-storage-settings.c \
	sieve-storage-sync.c \
	sieve-ast.c \
	sieve-binary.c \
	sieve-binary-file.c \
	sieve-binary-code.c \
	sieve-binary-debug.c \
	sieve-parser.c \
	sieve-address.c \
	sieve-validator.c \
	sieve-generator.c \
	sieve-execute.c \
	sieve-interpreter.c \
	sieve-runtime-trace.c \
	sieve-code-dumper.c \
	sieve-binary-dumper.c \
	sieve-result.c \
	sieve-error.c \
	sieve-objects.c \
	sieve-stringlist.c \
	sieve-comparators.c \
	sieve-match-types.c \
	sieve-address-parts.c \
	sieve-address-source.c \
	sieve-match.c \
	sieve-commands.c \
	sieve-code.c \
	sieve-actions.c \
	sieve-extensions.c \
	sieve-plugins.c \
	$(comparators) \
	$(match_types) \
	$(tests) \
	$(commands) \
	$(extensions) \
	sieve.c

headers = \
	sieve-config.h \
	sieve-types.h \
	sieve-common.h \
	sieve-limits.h \
	sieve-settings.h \
	sieve-settings.old.h \
	sieve-message.h \
	sieve-smtp.h \
	sieve-lexer.h \
	sieve-script.h \
	sieve-script-private.h \
	sieve-storage.h \
	sieve-storage-private.h \
	sieve-storage-settings.h \
	sieve-ast.h \
	sieve-binary.h \
	sieve-binary-private.h \
	sieve-parser.h \
	sieve-address.h \
	sieve-validator.h \
	sieve-generator.h \
	sieve-execute.h \
	sieve-interpreter.h \
	sieve-runtime-trace.h \
	sieve-runtime.h \
	sieve-code-dumper.h \
	sieve-binary-dumper.h \
	sieve-dump.h \
	sieve-result.h \
	sieve-error.h \
	sieve-error-private.h \
	sieve-objects.h \
	sieve-stringlist.h \
	sieve-match.h \
	sieve-comparators.h \
	sieve-match-types.h \
	sieve-address-parts.h \
	sieve-address-source.h \
	sieve-commands.h \
	sieve-code.h \
	sieve-actions.h \
	sieve-extensions.h \
	sieve-plugins.h \
	sieve.h

pkginc_libdir=$(dovecot_pkgincludedir)/sieve
pkginc_lib_HEADERS = $(headers)
