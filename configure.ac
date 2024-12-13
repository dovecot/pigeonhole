AC_PREREQ([2.69])
m4_define([pigeonhole_VERSION],
    m4_esyscmd_s([build-aux/git-version-gen]))))

m4_define([pigeonhole_ABI_VERSION],
    m4_esyscmd_s([build-aux/git-abi-version-gen]))))


# Be sure to update ABI version also if anything changes that might require
# recompiling plugins. Most importantly that means if any structs are changed.
AC_INIT([dovecot-pigeonhole], [m4_defn([pigeonhole_VERSION])], [dovecot@dovecot.org])
AC_DEFINE_UNQUOTED([PIGEONHOLE_ABI_VERSION], [m4_defn([pigeonhole_ABI_VERSION])], [Pigeonhole ABI version])

AC_CONFIG_SRCDIR([src])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

# Autoheader is not needed and does more harm than good for this package. However, it is
# tightly integrated in autoconf/automake and therefore it is difficult not to use it. As
# a workaround we give autoheader a dummy config header to chew on and we handle the
# real config header ourselves.
AC_CONFIG_HEADERS([dummy-config.h pigeonhole-config.h])

AC_DEFINE_UNQUOTED(PIGEONHOLE_NAME, "Pigeonhole",
	[Define to the full name of Pigeonhole for Dovecot.])
AC_DEFINE_UNQUOTED(PIGEONHOLE_VERSION, "$PACKAGE_VERSION",
	[Define to the version of Pigeonhole for Dovecot.])

AM_INIT_AUTOMAKE([no-define foreign tar-ustar])
AM_SILENT_RULES([yes])

AM_MAINTAINER_MODE

AC_PROG_CC
AC_PROG_CPP
LT_INIT

# Couple with Dovecot
#

DC_DOVECOT
DC_DOVECOT_MODULEDIR
LIBDOVECOT_INCLUDE="$LIBDOVECOT_INCLUDE $LIBDOVECOT_STORAGE_INCLUDE"
CFLAGS="$CFLAGS -I\$(top_srcdir)"
LIBS="$DOVECOT_LIBS"
BINARY_LDFLAGS="$PIE_LDFLAGS $RELRO_LDFLAGS"
BINARY_CFLAGS="$PIE_CFLAGS"
AC_SUBST(BINARY_CFLAGS)
AC_SUBST(BINARY_LDFLAGS)
AC_SUBST(LIBDOVECOT_INCLUDE)

AC_SUBST(PIGEONHOLE_ASSET_VERSION, regexp(AC_PACKAGE_VERSION, [^\([0-9]+\)\.\([0-9]+\).\([0-9]+\)], [\1.\2.\3]))
AS_IF([test "$PIGEONHOLE_ASSET_VERSION" = "0.0.0"], [
  AC_SUBST([PIGEONHOLE_ASSET_VERSION], "main")
])
AS_IF([test "$PIGEONHOLE_ASSET_VERSION" = ""], [
   AC_SUBST([PIGEONHOLE_ASSET_VERSION], "main")
])

AS_IF([test $DOVECOT_PRO_BUILD = "1"], [
  AC_SUBST(PIGEONHOLE_ASSET_URL, "doc.dovecotpro.com")
], [
  AC_SUBST(PIGEONHOLE_ASSET_URL, "doc.dovecot.org")
])

# Define Sieve documentation install dir
#

sieve_docdir='${dovecot_docdir}/sieve'
AC_SUBST(sieve_docdir)
AM_MISSING_PROG([WGET], [wget])

# Extensions under development
#

AC_ARG_WITH(unfinished-features,
AS_HELP_STRING([--with-unfinished-features], [Build unfinished new features/extensions (default=no)]),
  TEST_WITH(unfinished_features, $withval),
  want_unfinished_features=no)
AM_CONDITIONAL(BUILD_UNFINISHED, test "$want_unfinished_features" = "yes")

if test "$want_unfinished_features" = "yes"; then
	AC_DEFINE(HAVE_SIEVE_UNFINISHED,,
		[Define to build unfinished features/extensions.])
fi

AC_ARG_WITH(docs,
AS_HELP_STRING([--with-docs], [Install documentation (default=yes)]),
  TEST_WITH(docs, $withval),
  want_docs=yes)
AM_CONDITIONAL(BUILD_DOCS, test "$want_docs" = "yes")

AC_ARG_WITH(managesieve,
AS_HELP_STRING([--with-managesieve], [Build ManageSieve service (default=yes)]),
  TEST_WITH(managesieve, $withval),
  want_managesieve=yes)
AM_CONDITIONAL(BUILD_MANAGESIEVE, test "$want_managesieve" = "yes")

AC_ARG_WITH(ldap,
AS_HELP_STRING([--with-ldap=yes|plugin], [Build with LDAP support (default=no)]),
  TEST_WITH(ldap, $withval, plugin),
  want_ldap=no)

# FIXME: Imported this from Dovecot auth for now. We're working on a proper
# lib-ldap, but, until then, some code is duplicated.
have_ldap=no
if test $want_ldap != no; then
	AC_CHECK_LIB(ldap, ldap_init, [
		AC_CHECK_HEADER(ldap.h, [
			AC_CHECK_LIB(ldap, ldap_initialize, :, [
				AC_MSG_ERROR([
					cannot build with LDAP support: function ldap_initialize() not found
					(OpenLDAP >= 2.4 required)
				])
			], $LDAP_LIBS)
			AC_CHECK_LIB(ldap, ldap_start_tls_s, [
				AC_DEFINE(LDAP_HAVE_START_TLS_S,, [Define if you have ldap_start_tls_s])
			])
			LDAP_LIBS="-lldap"
			AC_CHECK_LIB(ldap, ber_free, [
			  # do nothing, default is to add -lldap to LIBS
			  :
			], [
			  AC_CHECK_LIB(lber, ber_free, [
			    LDAP_LIBS="$LDAP_LIBS -llber"
			  ])
			])
			AC_SUBST(LDAP_LIBS)
			if test $want_ldap != plugin; then
				AC_DEFINE(SIEVE_BUILTIN_LDAP,, [LDAP support is built in])
			fi

  		AC_DEFINE(STORAGE_LDAP,, [Build with LDAP support])
			AC_CHECK_HEADERS(sasl.h sasl/sasl.h)
			have_ldap=yes
		], [
		  if test $want_ldap != auto; then
		    AC_MSG_ERROR([cannot build with LDAP support: ldap.h not found])
		  fi
		])
	], [
	  if test $want_ldap != auto; then
	    AC_MSG_ERROR([cannot build with LDAP support: libldap not found])
	  fi
	])
fi

if test $have_ldap = no; then
  not_scriptloc="$not_scriptloc ldap"
else
  scriptloc="$scriptloc ldap"
  if test $want_ldap = plugin; then
    have_ldap_plugin=yes
    scriptloc="$scriptloc (plugin)"
  fi
fi
AM_CONDITIONAL(LDAP_PLUGIN, test "$have_ldap_plugin" = "yes")

dnl **
dnl ** Settings
dnl **

libsieve_setting_dirs="\
  $srcdir/src/lib-sieve/storage/data \
  $srcdir/src/lib-sieve/storage/file \
  $srcdir/src/lib-sieve/storage/dict \
  $srcdir/src/lib-sieve/plugins/vacation \
  $srcdir/src/lib-sieve/plugins/subaddress \
  $srcdir/src/lib-sieve/plugins/comparator-i-ascii-numeric \
  $srcdir/src/lib-sieve/plugins/relational \
  $srcdir/src/lib-sieve/plugins/regex \
  $srcdir/src/lib-sieve/plugins/copy \
  $srcdir/src/lib-sieve/plugins/imap4flags \
  $srcdir/src/lib-sieve/plugins/include \
  $srcdir/src/lib-sieve/plugins/body \
  $srcdir/src/lib-sieve/plugins/variables \
  $srcdir/src/lib-sieve/plugins/enotify \
  $srcdir/src/lib-sieve/plugins/environment \
  $srcdir/src/lib-sieve/plugins/mailbox \
  $srcdir/src/lib-sieve/plugins/date \
  $srcdir/src/lib-sieve/plugins/spamvirustest \
  $srcdir/src/lib-sieve/plugins/ihave \
  $srcdir/src/lib-sieve/plugins/editheader \
  $srcdir/src/lib-sieve/plugins/duplicate \
  $srcdir/src/lib-sieve/plugins/index \
  $srcdir/src/lib-sieve/plugins/metadata \
  $srcdir/src/lib-sieve/plugins/mime \
  $srcdir/src/lib-sieve/plugins/special-use \
  $srcdir/src/lib-sieve/plugins/vnd.dovecot/debug \
  $srcdir/src/lib-sieve/plugins/vnd.dovecot/environment \
  $srcdir/src/lib-sieve/plugins/vnd.dovecot/report"
if test $want_ldap != plugin; then
  libsieve_setting_dirs="$libsieve_setting_dirs \
    $srcdir/src/lib-sieve/storage/ldap"
fi
libsieve_headers=
libsieve_c_files=
non_libsieve_headers=
non_libsieve_c_files=
all_files=`find $srcdir/src -name '*.[[ch]]' | grep -v '/src/plugins/settings/' | grep -v '/src/managesieve' | grep -v '/test-' | xargs grep '\\(struct setting_parser_info [[a-z]]\\)\\|\\(struct service_settings [[a-z]]\\)\\|\\(<settings checks>\\)' | sed 's/:.*//' | sort | uniq`
for file in $all_files; do
  dir=`echo "$file" | sed 's:/[[^/]]*$::'`
  if echo "$libsieve_setting_dirs" | grep "$dir" >/dev/null; then
    dnl lib-sieve
    if echo "$file" | grep '\.h$' >/dev/null; then
      libsieve_headers="$libsieve_headers $file"
    else
      libsieve_c_files="$libsieve_c_files $file"
    fi
  else
    dnl not lib-sieve
    if echo "$file" | grep '\.h$' >/dev/null; then
      non_libsieve_headers="$non_libsieve_headers $file"
    else
      non_libsieve_c_files="$non_libsieve_c_files $file"
    fi
  fi
done

dnl libsieve is linked to pigeonhole-settings plugin, so don't duplicate the .c
dnl file contents. However, we still need to scan the .c files for any
dnl setting_parser_infos that aren't elsewhere.

# list headers first, C files last
SETTING_FILES=`echo $libsieve_headers $non_libsieve_headers $non_libsieve_c_files | sed -e s,$srcdir/src,./src,g -e 's,./src,$(top_srcdir)/src,g'`
SETTING_LINKED_FILES=`echo $libsieve_c_files | sed -e s,$srcdir/src,./src,g -e 's,./src,$(top_srcdir)/src,g'`
AC_SUBST(SETTING_FILES)
AC_SUBST(SETTING_LINKED_FILES)

CFLAGS="$CFLAGS $EXTRA_CFLAGS"
LDFLAGS="$LDFLAGS $EXTRA_LDFLAGS"

AS_IF([test "$AR_FLAGS" = "cru"], [AR_FLAGS="cr"], [])
AC_SUBST([AR_FLAGS])

AC_CONFIG_FILES([
Makefile
doc/Makefile
doc/man/Makefile
doc/example-config/Makefile
doc/example-config/conf.d/Makefile
doc/rfc/Makefile
doc/extensions/Makefile
doc/locations/Makefile
doc/plugins/Makefile
src/Makefile
src/lib-sieve/Makefile
src/lib-sieve/util/Makefile
src/lib-sieve/storage/Makefile
src/lib-sieve/storage/data/Makefile
src/lib-sieve/storage/file/Makefile
src/lib-sieve/storage/dict/Makefile
src/lib-sieve/storage/ldap/Makefile
src/lib-sieve/plugins/Makefile
src/lib-sieve/plugins/vacation/Makefile
src/lib-sieve/plugins/subaddress/Makefile
src/lib-sieve/plugins/comparator-i-ascii-numeric/Makefile
src/lib-sieve/plugins/relational/Makefile
src/lib-sieve/plugins/regex/Makefile
src/lib-sieve/plugins/imap4flags/Makefile
src/lib-sieve/plugins/copy/Makefile
src/lib-sieve/plugins/include/Makefile
src/lib-sieve/plugins/body/Makefile
src/lib-sieve/plugins/variables/Makefile
src/lib-sieve/plugins/enotify/Makefile
src/lib-sieve/plugins/enotify/mailto/Makefile
src/lib-sieve/plugins/environment/Makefile
src/lib-sieve/plugins/mailbox/Makefile
src/lib-sieve/plugins/date/Makefile
src/lib-sieve/plugins/spamvirustest/Makefile
src/lib-sieve/plugins/ihave/Makefile
src/lib-sieve/plugins/editheader/Makefile
src/lib-sieve/plugins/metadata/Makefile
src/lib-sieve/plugins/duplicate/Makefile
src/lib-sieve/plugins/index/Makefile
src/lib-sieve/plugins/mime/Makefile
src/lib-sieve/plugins/special-use/Makefile
src/lib-sieve/plugins/vnd.dovecot/Makefile
src/lib-sieve/plugins/vnd.dovecot/debug/Makefile
src/lib-sieve/plugins/vnd.dovecot/environment/Makefile
src/lib-sieve/plugins/vnd.dovecot/report/Makefile
src/lib-sieve-tool/Makefile
src/lib-managesieve/Makefile
src/plugins/Makefile
src/plugins/doveadm-sieve/Makefile
src/plugins/lda-sieve/Makefile
src/plugins/sieve-extprograms/Makefile
src/plugins/imapsieve/Makefile
src/plugins/imap-filter-sieve/Makefile
src/plugins/settings/Makefile
src/sieve-tools/Makefile
src/managesieve/Makefile
src/managesieve-login/Makefile
src/testsuite/Makefile
build-aux/run-test.sh
stamp.h])

AC_OUTPUT

not_scriptloc=`echo "$not_scriptloc"|sed 's/ / -/g'`

echo
echo "Install prefix . : $prefix"
echo "script drivers . : file dict$scriptloc"
if test "$not_scriptloc" != ""; then
  echo "                 :$not_scriptloc"
fi

