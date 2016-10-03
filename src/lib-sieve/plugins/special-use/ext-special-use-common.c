/* Copyright (c) 2019 Pigeonhole authors, see the included COPYING file */

#include "lib.h"
#include "imap-arg.h"

#include "ext-special-use-common.h"

bool ext_special_use_flag_valid(const char *flag)
{
	const char *p = flag;

	/* RFC 6154, Section 6:

	   use-attr        =  "\All" / "\Archive" / "\Drafts" / "\Flagged" /
	                      "\Junk" / "\Sent" / "\Trash" / use-attr-ext
	   use-attr-ext    =  "\" atom
	 */

	/* "\" */
	if (*p != '\\')
		return FALSE;
	p++;

	/* atom */
	for (; *p != '\0'; p++) {
		if (!IS_ATOM_CHAR(*p))
			return FALSE;
	}

	return TRUE;
}
