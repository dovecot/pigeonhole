/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __EDIT_MAIL_H
#define __EDIT_MAIL_H

struct edit_mail;

struct edit_mail *edit_mail_wrap(struct mail *mail);
void edit_mail_unwrap(struct edit_mail **edmail);
struct edit_mail *edit_mail_snapshot(struct edit_mail *edmail);

void edit_mail_reset(struct edit_mail *edmail);

struct mail *edit_mail_get_mail(struct edit_mail *edmail);

/*
 * Header modification
 */

/* Simple API */

void edit_mail_header_add
	(struct edit_mail *edmail, const char *field_name, const char *value,
		bool last);
int edit_mail_header_delete
	(struct edit_mail *edmail, const char *field_name, int index);

/* Iterator */

struct edit_mail_header_iter;

int edit_mail_headers_iterate_init
	(struct edit_mail *edmail, const char *field_name, bool reverse,
		struct edit_mail_header_iter **edhiter_r);
void edit_mail_headers_iterate_deinit
	(struct edit_mail_header_iter **edhiter);

void edit_mail_headers_iterate_get
	(struct edit_mail_header_iter *edhiter, const char **value_r);

bool edit_mail_headers_iterate_next
	(struct edit_mail_header_iter *edhiter);
bool edit_mail_headers_iterate_remove
	(struct edit_mail_header_iter *edhiter);



#endif /* __edit_mail_H */
