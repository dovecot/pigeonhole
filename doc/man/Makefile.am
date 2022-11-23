static_mans = \
	sieved.1

dist_man1_MANS := $(wildcard *.1)
dist_man7_MANS := $(wildcard *.7)

distclean-local:
	for page in *.1 *.7; do \
	  if ! echo "$(static_mans)" | grep -qF "$$page"; then \
	    rm -f $$page; \
	  fi \
	done

