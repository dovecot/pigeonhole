#include <string.h>

//#include "sieve-address.h"

/* FIXME: Currently accepts only c-strings and no \0 characters (not according to spec) 
 */

#ifdef TEST
#include <stdio.h>

typedef enum { FALSE, TRUE } bool;
#endif

/* Sieve address as defined in RFC3028 [SIEVE] and RFC822 [IMAIL]:
 *
 * sieve-address      = addr-spec                                     ; simple address
 *                    / phrase "<" addr-spec ">"                      ; name & addr-spec
 * addr-spec          =  local-part "@" domain                        ; global address
 * local-part         =  word *("." word)                             ; uninterpreted
 *                                                                    ; case-preserved
 * domain             =  sub-domain *("." sub-domain)
 * sub-domain         =  domain-ref / domain-literal
 * domain-literal     =  "[" *(dtext / quoted-pair) "]"
 * domain-ref         =  atom 
 * dtext              =  <any CHAR excluding "[", "]", "\" & CR, &    ; => may be folded
 *                       including linear-white-space>
 * phrase             =  1*word                                       ; Sequence of words
 * word               =  atom / quoted-string
 * quoted-string      = <"> *(qtext/quoted-pair) <">                  ; Regular qtext or
 *                                                                    ;   quoted chars.
 * quoted-pair        =  "\" CHAR                                     ; may quote any char
 * qtext              =  <any CHAR excepting <">,                     ; => may be folded
 *                       "\" & CR, and including
 *                       linear-white-space>
 * atom               =  1*<any CHAR except specials, SPACE and CTLs>
 * specials           =  "(" / ")" / "<" / ">" / "@"                  ; Must be in quoted-
 *                    /  "," / ";" / ":" / "\" / <">                  ;  string, to use
 *                    /  "." / "[" / "]"                              ;  within a word.
 * linear-white-space =  1*([CRLF] LWSP-char)                         ; semantics = SPACE
 *                                                                    ; CRLF => folding
 * LWSP-char          =  SPACE / HTAB                                 ; semantics = SPACE
 * CTL                =  <any ASCII control                           ; (  0- 37,  0.- 31.)
 *                       character and DEL>                           ; (    177,     127.)
 * CR                 =  <ASCII CR, carriage return>                  ; (     15,      13.)
 * SPACE              =  <ASCII SP, space>                            ; (     40,      32.)
 * HTAB               =  <ASCII HT, horizontal-tab>                   ; (     11,       9.)
 * CHAR               =  <any ASCII character>                        ; (  0-177,  0.-127.)
 * <">                =  <ASCII quote mark>                           ; (     42,      34.)
 *  
 * Note:  For purposes of display, and when passing  such  structured information to 
 *        other systems, such as mail protocol  services,  there  must  be  NO  
 *        linear-white-space between  <word>s  that are separated by period (".") or
 *        at-sign ("@") and exactly one SPACE between  all  other <word>s. 
 */

#define IS_CHAR(c) ( c <= 127 )
#define IS_NOT_CTL(c) ( c > 31 && c < 127 )
#define IS_SPACE(c) ( c == ' ' )
#define IS_SPECIAL(c) ( strchr( "()<>@,;:\\\".[]", c) != NULL )
#define IS_ATOM_CHAR(c) ( IS_NOT_CTL(c) && !IS_SPECIAL(c) && !IS_SPACE(c) )
#define IS_QTEXT_CHAR(c) ( IS_CHAR(c) && c != '"' && c != '\\' && c != '\r' )
#define IS_DTEXT_CHAR(c) ( IS_CHAR(c) && c != '[' && c != ']' && c != '\\' && c != '\r' )

/* Useful macro's to manipulate the pointer, also prevents reading beyond end of string */
#define shift(p) { if (**p == '\0') return FALSE; else printf("%c\n", **p); (*p)++; }
#define cur(p) (**p)

static bool parse_subdomain(const unsigned char **input) {
	if ( cur(input) == '[' ) {
  	/* Parse quoted-string */
  	shift(input);
  	while ( TRUE ) {
  		if ( cur(input) == '\\' ) {
  			shift(input);
  			shift(input);
  		} else if ( IS_DTEXT_CHAR(cur(input)) ) {
  			shift(input);
  		} else  
  			break;
  	}
  	
  	if ( cur(input) != ']' ) return FALSE;
  	
  	shift(input);
  	return TRUE;
	} else if ( IS_ATOM_CHAR(cur(input)) ) {
		/* Parse atom */
		while ( IS_ATOM_CHAR(cur(input)) ) {
			shift(input);
		}
		
		return TRUE;
	}
	
	return FALSE;
}

static bool parse_word(const unsigned char **input) {
  if ( cur(input) == '"' ) {
  	/* Parse quoted-string */
  	shift(input);
  	while ( TRUE ) {
  		if ( cur(input) == '\\' ) {
  			shift(input);
  			shift(input);
  		} else if ( IS_QTEXT_CHAR(cur(input)) ) {
  			shift(input);
  		} else  
  			break;
  	}
  	
  	if ( cur(input) != '"' ) return FALSE;
  	
  	shift(input);
  	return TRUE;
	} else if ( IS_ATOM_CHAR(cur(input)) ) {
		/* Parse atom */
		while ( IS_ATOM_CHAR(cur(input)) ) {
			shift(input);
		}
		
		return TRUE;
	}
	
	return FALSE;
}

bool sieve_address_validate(const unsigned char *address) 
{
	/* Parse local part */
	if ( parse_word(&address) ) {
		while ( cur(&address) == '.' ) {
			shift(&address)
			if ( !parse_word(&address) ) {
				return FALSE;
			}
		}
	} else return FALSE;
	 
	if ( cur(&address) == '@' ) {
		shift(&address);
		
		if ( parse_subdomain(&address) ) {
			while ( cur(&address) == '.' ) {
				shift(&address)
				if ( !parse_subdomain(&address) ) {
					return FALSE;
				}
			}
		} else return FALSE;
		
		return TRUE;
	}
	
	return FALSE;
}

#ifdef TEST /* TEST - gcc -DTEST -o test sieve-address.c */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	if ( argc != 2 ) {
		printf("Usage: test <address>\n");
		exit(1);
	}
	
	if ( sieve_address_validate(argv[1]) ) 
		printf("Addres valid.\n");
	else
		printf("Addres invalid.\n");
}

#endif 

