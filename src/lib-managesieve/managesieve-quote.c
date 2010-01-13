/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "managesieve-parser.h"
#include "managesieve-quote.h"

/* Turn the value string into a valid MANAGESIEVE string or literal, no matter 
 * what. QUOTED-SPECIALS are escaped, but any invalid (UTF-8) character
 * is simply removed. Linebreak characters are not considered invalid, but
 * they do force the generation of a string literal.
 */
void managesieve_quote_append(string_t *str, const unsigned char *value,
		       size_t value_len, bool compress_lwsp)
{
	size_t i, extra = 0;
	bool 
		last_lwsp = TRUE, 
		literal = FALSE, 
		modify = FALSE,
		escape = FALSE;
	int utf8_len;

 	if (value == NULL) {
		str_append(str, "\"\"");
		return;
	}

	if (value_len == (size_t)-1)
		value_len = strlen((const char *) value);

	for (i = 0; i < value_len; i++) {
		switch (value[i]) {
		case ' ':
		case '\t':
			if (last_lwsp && compress_lwsp) {
				modify = TRUE;
				extra++;
			}
			last_lwsp = TRUE;
			break;
		case '"':
		case '\\':
			escape = TRUE;
			last_lwsp = FALSE;
			break;
		case 13:
		case 10:
			literal = TRUE;
			last_lwsp = TRUE;
			break;
		default:
			/* Enforce valid UTF-8
			 */
			if ( (utf8_len=UTF8_LEN(value[i])) == 0 ) {
				modify = TRUE;
				extra++;
				break;
			}

			if ( utf8_len > 1 ) {
				int c = utf8_len - 1;

		 		if ( (i+utf8_len-1) >= value_len ) {
				  	/* Value ends in the middle of a UTF-8 character;
					 * Kill the partial UTF-8 character
					 */
				  	extra += i + utf8_len - value_len;
					modify = TRUE;
					break;        	
				}

				/* Parse the series of UTF8_1 characters */
				for (i++; c > 0; c--, i++ ) {
					if (!IS_UTF8_1(value[i])) {
						extra += utf8_len - c;
						modify = TRUE;
						break;
					}
				}
			}
   			
			last_lwsp = FALSE;
		}
	}

	if (!literal) {
		/* no linebreak chars, return as (escaped) "string" */
		str_append_c(str, '"');
	} else {
		/* return as literal */
		str_printfa(str, "{%"PRIuSIZE_T"}\r\n", value_len - extra);
	}

	if (!modify && (literal || !escape))
		str_append_n(str, value, value_len);
	else {
		last_lwsp = TRUE;
		for (i = 0; i < value_len; i++) {
			switch (value[i]) {
			case '"':
			case '\\':
				last_lwsp = FALSE;
				if (!literal) 
					str_append_c(str, '\\');
				str_append_c(str, value[i]);
				break;
			case ' ':
			case '\t':
				if (!last_lwsp || !compress_lwsp)
					str_append_c(str, ' ');
				last_lwsp = TRUE;
				break;
			case 13:
			case 10:
				last_lwsp = TRUE;
				str_append_c(str, value[i]);
				break;
			default:
	  			/* Enforce valid UTF-8
				 */
				if ( (utf8_len=UTF8_LEN(value[i])) == 0 ) 
					break;
      
				if ( utf8_len > 1 ) {
					int c = utf8_len - 1;
					int j;

					if ( (i+utf8_len-1) >= value_len ) {
						/* Value ends in the middle of a UTF-8 character;
						 * Kill the partial character
						 */
					 	i = value_len;
						break;
					}

					/* Parse the series of UTF8_1 characters */
					for (j = i+1; c > 0; c--, j++ ) {
						if (!IS_UTF8_1(value[j])) {
							/* Skip until after this erroneous character */
							i = j;
							break;
						}
					}

					/* Append the UTF-8 character. Last octet is done later */
					c = utf8_len - 1;
					for (; c > 0; c--, i++ ) 
						str_append_c(str, value[i]);
				}
     
				last_lwsp = FALSE;
				str_append_c(str, value[i]);
				break;
			}
		}
	}

	if (!literal)
		str_append_c(str, '"');
}

char *managesieve_quote(pool_t pool, const unsigned char *value, size_t value_len)
{
	string_t *str;
	char *ret;

	if (value == NULL)
		return "\"\"";

	T_BEGIN {
		str = t_str_new(value_len + MAX_INT_STRLEN + 5);
		managesieve_quote_append(str, value, value_len, TRUE);
		ret = p_strndup(pool, str_data(str), str_len(str));
	} T_END;

	return ret;
}
