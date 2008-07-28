/* Match-type ':matches' 
 */

#include "lib.h"
#include "str.h"

#include "sieve-match-types.h"
#include "sieve-comparators.h"
#include "sieve-match.h"

#include <string.h>
#include <stdio.h>

/*
 * Forward declarations
 */

static int mcht_matches_match
	(struct sieve_match_context *mctx, const char *val, size_t val_size, 
		const char *key, size_t key_size, int key_index);

/*
 * Match-type object
 */

const struct sieve_match_type matches_match_type = {
	SIEVE_OBJECT("matches", &match_type_operand, SIEVE_MATCH_TYPE_MATCHES),
	TRUE, FALSE,
	NULL,
	sieve_match_substring_validate_context, 
	NULL,
	mcht_matches_match,
	NULL
};

/*
 * Match-type implementation
 */

/* Quick 'n dirty debug */
//#define MATCH_DEBUG
#ifdef MATCH_DEBUG
#define debug_printf(...) printf ("match debug: " __VA_ARGS__)
#else
#define debug_printf(...) 
#endif

/* FIXME: Naive implementation, substitute this with dovecot src/lib/str-find.c
 */
static inline bool _string_find(const struct sieve_comparator *cmp, 
	const char **valp, const char *vend, const char **keyp, const char *kend)
{
	while ( (*valp < vend) && (*keyp < kend) ) {
		if ( !cmp->char_match(cmp, valp, vend, keyp, kend) )
			(*valp)++;
	}
	
	return (*keyp == kend);
}

static char _scan_key_section
	(string_t *section, const char **wcardp, const char *key_end)
{
	/* Find next wildcard and resolve escape sequences */	
	str_truncate(section, 0);
	while ( *wcardp < key_end && **wcardp != '*' && **wcardp != '?') {
		if ( **wcardp == '\\' ) {
			(*wcardp)++;
		}
		str_append_c(section, **wcardp);
		(*wcardp)++;
	}
	
	/* Record wildcard character or \0 */
	if ( *wcardp < key_end ) {			
		return **wcardp;
	} 
	
	i_assert( *wcardp == key_end );
	return '\0';
}

static int mcht_matches_match
(struct sieve_match_context *mctx, const char *val, size_t val_size, 
	const char *key, size_t key_size, int key_index ATTR_UNUSED)
{
	const struct sieve_comparator *cmp = mctx->comparator;
	struct sieve_match_values *mvalues;
	
	string_t *mvalue = t_str_new(32);     /* Match value (*) */
	string_t *mchars = t_str_new(32);     /* Match characters (.?..?.??) */
	string_t *section = t_str_new(32);    /* Section (after beginning or *) */
	string_t *subsection = t_str_new(32); /* Sub-section (after ?) */
	
	const char *vend = (const char *) val + val_size;
	const char *kend = (const char *) key + key_size;
	const char *vp = val;   /* Value pointer */
	const char *kp = key;   /* Key pointer */
	const char *wp = key;   /* Wildcard (key) pointer */
	const char *pvp = val;  /* Previous value Pointer */
	
	bool backtrack = FALSE; /* TRUE: match of '?'-connected sections failed */
	char wcard = '\0';      /* Current wildcard */
	char next_wcard = '\0'; /* Next  widlcard */
	unsigned int key_offset = 0;
	unsigned int j = 0;
	
	/* Reset match values list */
	mvalues = sieve_match_values_start(mctx->interp);
	sieve_match_values_add(mvalues, NULL);

	/* Match the pattern: 
	 *   <pattern> = <section>*<section>*<section>....
	 *   <section> = [text]?[text]?[text].... 
	 *
	 * Escape sequences \? and \* need special attention. 
	 */
	 
	debug_printf("=== Start ===\n");
	debug_printf("  key:   %s\n", t_strdup_until(key, kend));
	debug_printf("  value: %s\n", t_strdup_until(val, vend));

	/* Loop until either key or value ends */
	while (kp < kend && vp < vend && j < 40) {
		const char *needle, *nend;
		
		if ( !backtrack ) {
			wcard = next_wcard;
			
			/* Find the needle to look for in the string */	
			key_offset = 0;	
			for (;;) {
				next_wcard = _scan_key_section(section, &wp, kend);
				
				if ( wcard == '\0' || str_len(section) > 0 ) 
					break;
					
				if ( next_wcard == '*' ) {	
					break;
				}
					
				if ( wp < kend ) 
					wp++;
				else 
					break;
				key_offset++;
			}
			
			debug_printf("found wildcard '%c' at pos [%d]\n", 
				next_wcard, (int) (wp-key));
				
			str_truncate(mvalue, 0);
		} else {
			debug_printf("backtracked");
			backtrack = FALSE;
		}
		
		needle = str_c(section);
		nend = PTR_OFFSET(needle, str_len(section));		
		 
		debug_printf("  section needle:  '%s'\n", t_strdup_until(needle, nend));
		debug_printf("  section key:     '%s'\n", t_strdup_until(kp, kend));
		debug_printf("  section remnant: '%s'\n", t_strdup_until(wp, kend));
		debug_printf("  value remnant:   '%s'\n", t_strdup_until(vp, vend));
		
		pvp = vp;
		if ( next_wcard == '\0' ) {			
			const char *qp, *qend;
			
			debug_printf("next_wcard = NUL; must find needle at end\n");				 
			
			/* Find substring at the end of string */
			if ( vend - str_len(section) < vp ) {
				debug_printf("  wont match: value is too short\n");
				break;
			}

			vp = PTR_OFFSET(vend, -str_len(section));
			qend = vp;
			qp = vp - key_offset;
			str_append_n(mvalue, pvp, qp-pvp);
					
			if ( !cmp->char_match(cmp, &vp, vend, &needle, nend) ) {	
				debug_printf("  match at end failed\n");				 
				break;
			}
			
			sieve_match_values_add(mvalues, mvalue);
			for ( ; qp < qend; qp++ )
				sieve_match_values_add_char(mvalues, *qp); 

			kp = kend;
			vp = vend;
			debug_printf("  matched end of value\n");
			break;
		} else {
			const char *prv = NULL;
			const char *prk = NULL;
			const char *prw = NULL;
			const char *chars;
		
			str_truncate(mchars, 0);
							
			if ( wcard == '\0' ) {
				/* Match needs to happen right at the beginning */
				debug_printf("wcard = NUL; needle should be found at the beginning.\n");
				debug_printf("  begin needle: '%s'\n", t_strdup_until(needle, nend));
				debug_printf("  begin value:  '%s'\n", t_strdup_until(vp, vend));

				if ( !cmp->char_match(cmp, &vp, vend, &needle, nend) ) {	
					debug_printf("  failed to find needle at begin\n");				 
					break;
				}

			} else {
				const char *qp, *qend;

				debug_printf("wcard != NUL; must find needle at an offset.\n");
				
				/* Match may happen at any offset: find substring */
				if ( !_string_find(cmp, &vp, vend, &needle, nend)	) {
					debug_printf("  failed to find needle at an offset\n"); 
					break;
				}
			
				prv = vp - str_len(section);
				prk = kp;
				prw = wp;
				
				qend = vp - str_len(section);
				qp = qend - key_offset;
				str_append_n(mvalue, pvp, qp-pvp);
				for ( ; qp < qend; qp++ )
					str_append_c(mchars, *qp);
			}
			
			if ( wp < kend ) wp++;
			kp = wp;
		
			while ( next_wcard == '?' ) {
				debug_printf("next_wcard = '?'; need to match arbitrary character\n");
				
				/* Add match value */ 
				str_append_c(mchars, *vp);
				vp++;
				
				next_wcard = _scan_key_section(subsection, &wp, kend);
				debug_printf("found next wildcard '%c' at pos [%d] (fixed match)\n", 
					next_wcard, (int) (wp-key));
					
				needle = str_c(subsection);
				nend = PTR_OFFSET(needle, str_len(subsection));

				debug_printf("  sub key:       '%s'\n", t_strdup_until(needle, nend));
				debug_printf("  value remnant: '%s'\n", vp <= vend ? t_strdup_until(vp, vend) : "");

				if ( (needle == nend && next_wcard == '\0' && vp < vend ) || 
					!cmp->char_match(cmp, &vp, vend, &needle, nend) ) {	
					debug_printf("  failed fixed match\n");

					if ( prv != NULL && prv + 1 < vend ) {
						vp = prv;
						kp = prk;
						wp = prw;
				
						str_append_c(mvalue, *vp);
						vp++;
				
						wcard = '*';
						next_wcard = '?';
				
						backtrack = TRUE;				 
						debug_printf("  BACKTRACK\n");
					}
					break;
				}
				
				if ( wp < kend ) wp++;
				kp = wp;
			}
			
			if ( !backtrack ) {
				unsigned int i;
				
				if ( next_wcard == '?' ) {
					debug_printf("failed to match '?'\n");	
					break;
				}
				
				if ( prv != NULL )
					sieve_match_values_add(mvalues, mvalue);
				chars = (const char *) str_data(mchars);
				for ( i = 0; i < str_len(mchars); i++ ) {
					sieve_match_values_add_char(mvalues, chars[i]);
				}

				if ( next_wcard != '*' ) {
					debug_printf("failed to match at end of string\n");
					break;
				}
			}
		}
					
		/* Check whether string ends in a wildcard 
		 * (avoid scanning the rest of the string)
		 */
		if ( kp == kend && next_wcard == '*' ) {
			str_truncate(mvalue, 0);
			str_append_n(mvalue, vp, vend-vp);
			sieve_match_values_add(mvalues, mvalue);
			kp = kend;
			vp = vend;
			debug_printf("key ends with '*'\n");
			break;
		}			
					
		debug_printf("== Loop ==\n");
		j++;
	}
	
	debug_printf("=== Finish ===\n");
	debug_printf("  result: %s\n", (kp == kend && vp == vend) ? "true" : "false");
	
	/* By definition, the match is only successful if both value and key pattern
	 * are exhausted.
	 */
	if (kp == kend && vp == vend) {
		string_t *matched = str_new_const(pool_datastack_create(), val, val_size);
		sieve_match_values_set(mvalues, 0, matched);

		return TRUE;
	}

	return FALSE;
}
			 
