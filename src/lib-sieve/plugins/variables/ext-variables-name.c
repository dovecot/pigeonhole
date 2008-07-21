#include "lib.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"

#include "ext-variables-common.h"
#include "ext-variables-name.h"

#include <ctype.h>

int ext_variable_name_parse
(ARRAY_TYPE(ext_variable_name) *vname, const char **str, const char *strend)
{
	const char *p = *str;
	int nspace_used = 0;
				
	for (;;) { 
		struct ext_variable_name *cur_element;
		string_t *cur_ident;

		/* Acquire current position in the substitution structure or allocate 
		 * a new one if this substitution consists of more elements than before.
		 */
		if ( nspace_used < (int) array_count(vname) ) {
			cur_element = array_idx_modifiable
				(vname, (unsigned int) nspace_used);
			cur_ident = cur_element->identifier;
		} else {
			cur_element = array_append_space(vname);
			cur_ident = cur_element->identifier = t_str_new(32);
		}

		/* Identifier */
		if ( *p == '_' || i_isalpha(*p) ) {
			cur_element->num_variable = -1;
			str_truncate(cur_ident, 0);
			str_append_c(cur_ident, *p);
			p++;
		
			while ( p < strend && (*p == '_' || i_isalnum(*p)) ) {
				str_append_c(cur_ident, *p);
				p++;
			}
		
		/* Num-variable */
		} else if ( i_isdigit(*p) ) {
			cur_element->num_variable = *p - '0';
			p++;
			
			while ( p < strend && i_isdigit(*p) ) {
				cur_element->num_variable = cur_element->num_variable*10 + (*p - '0');
				p++;
			} 

			/* If a num-variable is first, no more elements can follow because no
			 * namespace is specified.
			 */
			if ( nspace_used == 0 ) {
				*str = p;
				return 1;
			}
		} else {
			*str = p;
			return -1;
		}
		
		nspace_used++;
		
		if ( p < strend && *p == '.' ) 
			p++;
		else
			break;
	}
	
	*str = p;
	return nspace_used;
} 
 

