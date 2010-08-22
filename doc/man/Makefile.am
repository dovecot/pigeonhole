unfinished = sieve-filter.1

if BUILD_UNFINISHED
unfinished_mans = $(unfinished)
endif

dist_man1_MANS = \
	sieved.1

nodist_man1_MANS = \
	sievec.1 \
	sieve-dump.1 \
	sieve-test.1 \
	$(unfinished_mans)

nodist_man7_MANS = \
	pigeonhole.7

man_includefiles = \
	$(srcdir)/reporting-bugs.inc

EXTRA_DIST = \
	sievec.1.in \
	sieve-dump.1.in \
	sieve-test.1.in \
	sieve-filter.1.in \
	pigeonhole.7.in

CLEANFILES = $(nodist_man1_MANS) $(nodist_man7_MANS)

%.1: $(srcdir)/%.1.in $(man_includefiles) Makefile
	$(SHELL) $(srcdir)/sed.sh $(srcdir) $(rundir) $(pkgsysconfdir) \
		< $(srcdir)/$< > $@

%.7: $(srcdir)/%.7.in $(man_includefiles) Makefile
	$(SHELL) $(srcdir)/sed.sh $(srcdir) $(rundir) $(pkgsysconfdir) \
		< $(srcdir)/$< > $@
