unfinished = sieve-filter.1

if BUILD_UNFINISHED
unfinished_mans = $(unfinished)
else
unfinished_dist = $(unfinished)
endif

EXTRA_DIST = \
	$(unfinished_dist)

dist_man_MANS = \
	sievec.1 \
	sieved.1 \
	sieve-test.1 \
	$(unfinished_mans)

