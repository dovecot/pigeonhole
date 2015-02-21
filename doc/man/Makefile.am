pkgsysconfdir = $(sysconfdir)/dovecot
rundir = ${prefix}/var/run/dovecot

SUFFIXES = .1.in .1 .7.in .7

dist_man1_MANS = \
	sieved.1

nodist_man1_MANS = \
	doveadm-sieve.1 \
	sievec.1 \
	sieve-dump.1 \
	sieve-test.1 \
	sieve-filter.1

nodist_man7_MANS = \
	pigeonhole.7

man_includefiles = \
	$(srcdir)/global-options-formatter.inc \
	$(srcdir)/global-options.inc \
	$(srcdir)/option-A.inc \
	$(srcdir)/option-S-socket.inc \
	$(srcdir)/option-u-user.inc \
	$(srcdir)/reporting-bugs.inc

EXTRA_DIST = \
	doveadm-sieve.1.in \
	sievec.1.in \
	sieve-dump.1.in \
	sieve-test.1.in \
	sieve-filter.1.in \
	pigeonhole.7.in \
	sed.sh \
	$(man_includefiles)

CLEANFILES = $(nodist_man1_MANS) $(nodist_man7_MANS)

.1.in.1: $(man_includefiles) Makefile
	$(SHELL) $(srcdir)/sed.sh $(srcdir) $(rundir) $(pkgsysconfdir) \
		$(pkglibexecdir) < $< > $@
.7.in.7: $(man_includefiles) Makefile
	$(SHELL) $(srcdir)/sed.sh $(srcdir) $(rundir) $(pkgsysconfdir) \
		$(pkglibexecdir) < $< > $@


