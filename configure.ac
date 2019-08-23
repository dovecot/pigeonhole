AC_PREREQ([2.59])

# Be sure to update ABI version also if anything changes that might require
# recompiling plugins. Most importantly that means if any structs are changed.
AC_INIT([Pigeonhole], [0.5.7.2], [dovecot@dovecot.org], [dovecot-2.3-pigeonhole])
AC_DEFINE_UNQUOTED([PIGEONHOLE_ABI_VERSION], "0.5.ABIv7($PACKAGE_VERSION)", [Pigeonhole ABI version])

AC_CONFIG_AUX_DIR([.])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_MACRO_DIR([m4])

# Autoheader is not needed and does more harm than good for this package. However, it is
# tightly integrated in autoconf/automake and therefore it is difficult not to use it. As
# a workaround we give autoheader a dummy config header to chew on and we handle the
# real config header ourselves.
AC_CONFIG_HEADERS([dummy-config.h pigeonhole-config.h])

AC_DEFINE_UNQUOTED(PIGEONHOLE_NAME, "$PACKAGE_NAME",
	[Define to the full name of Pigeonhole for Dovecot.])
AC_DEFINE_UNQUOTED(PIGEONHOLE_VERSION, "$PACKAGE_VERSION",
	[Define to the version of Pigeonhole for Dovecot.])

AM_INIT_AUTOMAKE([no-define foreign tar-ustar])

AM_MAINTAINER_MODE

AC_PROG_CC
AC_PROG_CPP
AC_PROG_LIBTOOL

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

# Define Sieve documentation install dir
#

sieve_docdir='${dovecot_docdir}/sieve'
AC_SUBST(sieve_docdir)

# Extensions under development
#

AC_ARG_WITH(unfinished-features,
[AC_HELP_STRING([--with-unfinished-features],
	[Build unfinished new features/extensions [default=no]])],
        if test x$withval = xno || test x$withval = xauto; then
                want_unfinished_features=$withval
        else
                want_unfinished_features=yes
        fi,
        want_unfinished_features=no)
AM_CONDITIONAL(BUILD_UNFINISHED, test "$want_unfinished_features" = "yes")

if test "$want_unfinished_features" = "yes"; then
	AC_DEFINE(HAVE_SIEVE_UNFINISHED,,
		[Define to build unfinished features/extensions.])
fi

#
#

dnl TEST_WITH(name, value, [plugin])
AC_DEFUN([TEST_WITH], [
  want=want_`echo $1|sed s/-/_/g`
  if test $2 = yes || test $2 = no || test $2 = auto; then
    eval $want=$2
  elif test $2 = plugin; then
    if test "$3" = plugin; then
      eval $want=plugin
    else
      AC_ERROR([--with-$1=plugin not supported])
    fi
  elif `echo $2|grep '^/' >/dev/null`; then
    AC_ERROR([--with-$1=path not supported. You may want to use instead:
CPPFLAGS=-I$2/include LDFLAGS=-L$2/lib ./configure --with-$1])
  else
    AC_ERROR([--with-$1: Unknown value: $2])
  fi
])

AC_ARG_WITH(docs,
[  --with-docs             Install documentation (default)],
    if test x$withval = xno; then
        want_docs=no
    else
        want_docs=yes
    fi,
    want_docs=yes)
AM_CONDITIONAL(BUILD_DOCS, test "$want_docs" = "yes")

AC_ARG_WITH(managesieve,
[AC_HELP_STRING([--with-managesieve],
	[Build ManageSieve service [default=yes]])],
        if test x$withval = xno || test x$withval = xauto; then
                want_managesieve=$withval
        else
                want_managesieve=yes
        fi,
        want_managesieve=yes)
AM_CONDITIONAL(BUILD_MANAGESIEVE, test "$want_managesieve" = "yes")

AC_ARG_WITH(ldap,
AS_HELP_STRING([--with-ldap=yes|plugin], [Build with LDAP support]),
  TEST_WITH(ldap, $withval, plugin),
  want_ldap=no)

# FIXME: Imported this from Dovecot auth for now. We're working on a proper
# lib-ldap, but, until then, some code is duplicated.
have_ldap=no
if test $want_ldap != no; then
	AC_CHECK_LIB(ldap, ldap_init, [
		AC_CHECK_HEADER(ldap.h, [
			AC_CHECK_LIB(ldap, ldap_initialize, [
				AC_DEFINE(LDAP_HAVE_INITIALIZE,, [Define if you have ldap_initialize])
			])
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
		    AC_ERROR([Can't build with LDAP support: ldap.h not found])
		  fi
		])
	], [
	  if test $want_ldap != auto; then
	    AC_ERROR([Can't build with LDAP support: libldap not found])
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

CFLAGS="$CFLAGS $EXTRA_CFLAGS"
LDFLAGS="$LDFLAGS $EXTRA_LDFLAGS"

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
src/lib-sieve/plugins/notify/Makefile
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
stamp.h])

AC_OUTPUT

not_scriptloc=`echo "$not_scriptloc"|sed 's/ / -/g'`

echo
echo "Install prefix . : $prefix"
echo "script drivers . : file dict$scriptloc"
if test "$not_scriptloc" != ""; then
  echo "                 :$not_scriptloc"
fi

