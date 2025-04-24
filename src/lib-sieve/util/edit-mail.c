/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "str.h"
#include "mempool.h"
#include "llist.h"
#include "istream-private.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "message-parser.h"
#include "message-header-encode.h"
#include "message-header-decode.h"
#include "mail-user.h"
#include "mail-storage-service.h"
#include "mail-storage-private.h"
#include "index-mail.h"
#include "raw-storage.h"

#include "rfc2822.h"

#include "edit-mail.h"

/*
 * Forward declarations
 */

struct _header_field_index;
struct _header_field;
struct _header_index;
struct _header;

static struct mail_vfuncs edit_mail_vfuncs;

struct edit_mail_istream;
struct istream *edit_mail_istream_create(struct edit_mail *edmail);

static struct _header_index *
edit_mail_header_clone(struct edit_mail *edmail, struct _header *header);

/*
 * Raw storage
 */

static struct mail_user *edit_mail_user = NULL;
static unsigned int edit_mail_refcount = 0;

static struct mail_user *edit_mail_raw_storage_get(struct mail_user *mail_user)
{
	if (edit_mail_user == NULL) {
		struct mail_storage_service_ctx *storage_service =
			mail_storage_service_user_get_service_ctx(
				mail_user->service_user);
		struct settings_instance *set_instance =
			mail_storage_service_user_get_settings_instance(mail_user->service_user);
		edit_mail_user =
			raw_storage_create_from_set(storage_service, set_instance);
	}

	edit_mail_refcount++;

	return edit_mail_user;
}

static void edit_mail_raw_storage_drop(void)
{
	i_assert(edit_mail_refcount > 0);

	if (--edit_mail_refcount != 0)
		return;

	mail_user_unref(&edit_mail_user);
	edit_mail_user = NULL;
}

/*
 * Headers
 */

struct _header_field {
	struct _header *header;

	unsigned int refcount;

	char *data;
	size_t size;
	size_t virtual_size;
	uoff_t offset;
	unsigned int lines;

	uoff_t body_offset;

	char *utf8_value;
};

struct _header_field_index {
	struct _header_field_index *prev, *next;

	struct _header_field *field;
	struct _header_index *header;
};

struct _header {
	unsigned int refcount;

	char *name;
};

struct _header_index {
	struct _header_index *prev, *next;

	struct _header *header;

	struct _header_field_index *first, *last;

	unsigned int count;
};

static inline struct _header *_header_create(const char *name)
{
	struct _header *header;

	header = i_new(struct _header, 1);
	header->name = i_strdup(name);
	header->refcount = 1;

	return header;
}

static inline void _header_ref(struct _header *header)
{
	header->refcount++;
}

static inline void _header_unref(struct _header *header)
{
	i_assert(header->refcount > 0);
	if (--header->refcount != 0)
		return;

	i_free(header->name);
	i_free(header);
}

static inline struct _header_field *_header_field_create(struct _header *header)
{
	struct _header_field *hfield;

	hfield = i_new(struct _header_field, 1);
	hfield->refcount = 1;
	hfield->header = header;
	if (header != NULL)
		_header_ref(header);

	return hfield;
}

static inline void _header_field_ref(struct _header_field *hfield)
{
	hfield->refcount++;
}

static inline void _header_field_unref(struct _header_field *hfield)
{
	i_assert(hfield->refcount > 0);
	if (--hfield->refcount != 0)
		return;

	if (hfield->header != NULL)
		_header_unref(hfield->header);

	if (hfield->data != NULL)
		i_free(hfield->data);
	if (hfield->utf8_value != NULL)
		i_free(hfield->utf8_value);
	i_free(hfield);
}

/*
 * Edit mail object
 */

struct edit_mail {
	struct mail_private mail;
	struct mail_private *wrapped;

	struct edit_mail *parent;
	unsigned int refcount;

	struct istream *wrapped_stream;
	struct istream *stream;

	struct _header_index *headers_head, *headers_tail;
	struct _header_field_index *header_fields_head, *header_fields_tail;
	struct message_size hdr_size, body_size;

	struct message_size wrapped_hdr_size, wrapped_body_size;

	struct _header_field_index *header_fields_appended;
	struct message_size appended_hdr_size;

	bool modified:1;
	bool snapshot_modified:1;
	bool crlf:1;
	bool eoh_crlf:1;
	bool headers_parsed:1;
	bool destroying_stream:1;
};

struct edit_mail *edit_mail_wrap(struct mail *mail)
{
	struct mail_private *mailp = (struct mail_private *) mail;
	struct edit_mail *edmail;
	struct mail_user *raw_mail_user;
	struct mailbox *raw_box = NULL;
	struct mailbox_transaction_context *raw_trans;
	struct message_size hdr_size, body_size;
	struct istream *wrapped_stream;
	uoff_t size_diff;
	pool_t pool;

	if (mail_get_stream(mail, &hdr_size, &body_size, &wrapped_stream) < 0)
		return NULL;

	/* Create dummy raw mailbox for our wrapper */

	raw_mail_user = edit_mail_raw_storage_get(mail->box->storage->user);

	if (raw_mailbox_alloc_stream(raw_mail_user, wrapped_stream, (time_t)-1,
				     "editor@example.com", &raw_box) < 0) {
		i_error("edit-mail: failed to open raw box: %s",
			mailbox_get_last_internal_error(raw_box, NULL));
		mailbox_free(&raw_box);
		edit_mail_raw_storage_drop();
		return NULL;
	}

	raw_trans = mailbox_transaction_begin(raw_box, 0, __func__);

	/* Create the wrapper mail */

	pool = pool_alloconly_create("edit_mail", 1024);
	edmail = p_new(pool, struct edit_mail, 1);
	edmail->refcount = 1;
	edmail->mail.pool = pool;

	edmail->wrapped = mailp;
	edmail->wrapped_hdr_size = hdr_size;
	edmail->wrapped_body_size = body_size;

	edmail->wrapped_stream = wrapped_stream;
	i_stream_ref(edmail->wrapped_stream);

	/* Determine whether we should use CRLF or LF for the physical message
	 */
	size_diff = ((hdr_size.virtual_size + body_size.virtual_size) -
		     (hdr_size.physical_size + body_size.physical_size));
	if (size_diff == 0 || size_diff <= (hdr_size.lines + body_size.lines)/2)
		edmail->crlf = edmail->eoh_crlf = TRUE;

	array_create(&edmail->mail.module_contexts, pool, sizeof(void *), 5);

	edmail->mail.v = edit_mail_vfuncs;
	edmail->mail.mail.seq = 1;
	edmail->mail.mail.box = raw_box;
	edmail->mail.mail.transaction = raw_trans;
	edmail->mail.wanted_fields = mailp->wanted_fields;
	edmail->mail.wanted_headers = mailp->wanted_headers;

	return edmail;
}

struct edit_mail *edit_mail_snapshot(struct edit_mail *edmail)
{
	struct _header_field_index *field_idx, *field_idx_new;
	struct edit_mail *edmail_new;
	pool_t pool;

	if (!edmail->snapshot_modified)
		return edmail;

	pool = pool_alloconly_create("edit_mail", 1024);
	edmail_new = p_new(pool, struct edit_mail, 1);
	edmail_new->refcount = 1;
	edmail_new->mail.pool = pool;

	edmail_new->wrapped = edmail->wrapped;
	edmail_new->wrapped_hdr_size = edmail->wrapped_hdr_size;
	edmail_new->wrapped_body_size = edmail->wrapped_body_size;
	edmail_new->hdr_size = edmail->hdr_size;
	edmail_new->body_size = edmail->body_size;
	edmail_new->appended_hdr_size = edmail->appended_hdr_size;

	edmail_new->wrapped_stream = edmail->wrapped_stream;
	i_stream_ref(edmail_new->wrapped_stream);

	edmail_new->crlf = edmail->crlf;
	edmail_new->eoh_crlf = edmail->eoh_crlf;

	array_create(&edmail_new->mail.module_contexts, pool,
		     sizeof(void *), 5);

	edmail_new->mail.v = edit_mail_vfuncs;
	edmail_new->mail.mail.seq = 1;
	edmail_new->mail.mail.box = edmail->mail.mail.box;
	edmail_new->mail.mail.transaction = edmail->mail.mail.transaction;
	edmail_new->mail.wanted_fields = 	edmail->mail.wanted_fields;
	edmail_new->mail.wanted_headers = edmail->mail.wanted_headers;

	edmail_new->stream = NULL;

	if (edmail->modified) {
		field_idx = edmail->header_fields_head;
		while (field_idx != NULL) {
			struct _header_field_index *next = field_idx->next;

			field_idx_new = i_new(struct _header_field_index, 1);

			field_idx_new->header = edit_mail_header_clone(
				edmail_new, field_idx->header->header);

			field_idx_new->field = field_idx->field;
			_header_field_ref(field_idx_new->field);

			DLLIST2_APPEND(&edmail_new->header_fields_head,
				       &edmail_new->header_fields_tail,
				       field_idx_new);

			field_idx_new->header->count++;
			if (field_idx->header->first == field_idx)
				field_idx_new->header->first = field_idx_new;
			if (field_idx->header->last == field_idx)
				field_idx_new->header->last = field_idx_new;

			if (field_idx == edmail->header_fields_appended) {
				edmail_new->header_fields_appended =
					field_idx_new;
			}

			field_idx = next;
		}

		edmail_new->modified = TRUE;
	}

	edmail_new->headers_parsed = edmail->headers_parsed;
	edmail_new->parent = edmail;

	return edmail_new;
}

void edit_mail_reset(struct edit_mail *edmail)
{
	struct _header_index *header_idx;
	struct _header_field_index *field_idx;

	i_stream_unref(&edmail->stream);

	field_idx = edmail->header_fields_head;
	while (field_idx != NULL) {
		struct _header_field_index *next = field_idx->next;

		_header_field_unref(field_idx->field);
		i_free(field_idx);

		field_idx = next;
	}

	header_idx = edmail->headers_head;
	while (header_idx != NULL) {
		struct _header_index *next = header_idx->next;

		_header_unref(header_idx->header);
		i_free(header_idx);

		header_idx = next;
	}

	edmail->modified = FALSE;
}

void edit_mail_unwrap(struct edit_mail **edmail)
{
	struct edit_mail *parent;

	i_assert((*edmail)->refcount > 0);
	if (--(*edmail)->refcount != 0)
		return;

	edit_mail_reset(*edmail);
	i_stream_unref(&(*edmail)->wrapped_stream);

	parent = (*edmail)->parent;

	if (parent == NULL) {
		mailbox_transaction_rollback(&(*edmail)->mail.mail.transaction);
		mailbox_free(&(*edmail)->mail.mail.box);
		edit_mail_raw_storage_drop();
	}

	pool_unref(&(*edmail)->mail.pool);
	*edmail = NULL;

	if (parent != NULL)
		edit_mail_unwrap(&parent);
}

struct mail *edit_mail_get_mail(struct edit_mail *edmail)
{
	/* Return wrapped mail when nothing is modified yet */
	if (!edmail->modified)
		return &edmail->wrapped->mail;

	return &edmail->mail.mail;
}

/*
 * Editing
 */

static inline void edit_mail_modify(struct edit_mail *edmail)
{
	edmail->mail.mail.seq++;
	edmail->modified = TRUE;
	edmail->snapshot_modified = TRUE;
}

/* Header modification */

static inline char *_header_value_unfold(const char *value)
{
	string_t *out;
	unsigned int i;

	for (i = 0; value[i] != '\0'; i++) {
		if (value[i] == '\r' || value[i] == '\n')
			break;
	}
	if (value[i] == '\0')
		return i_strdup(value);

	out = t_str_new(i + strlen(value+i) + 10);
	str_append_data(out, value, i);
	for (; value[i] != '\0'; i++) {
		if (value[i] == '\n') {
			i++;
			if (value[i] == '\0')
				break;

			switch (value[i]) {
			case ' ':
				str_append_c(out, ' ');
				break;
			case '\t':
			default:
				str_append_c(out, '\t');
			}
		} else {
			if (value[i] != '\r')
				str_append_c(out, value[i]);
		}
	}

	return i_strndup(str_c(out), str_len(out));
}

static struct _header_index *
edit_mail_header_find(struct edit_mail *edmail, const char *field_name)
{
	struct _header_index *header_idx;

	header_idx = edmail->headers_head;
	while (header_idx != NULL) {
		if (strcasecmp(header_idx->header->name, field_name) == 0)
			return header_idx;

		header_idx = header_idx->next;
	}

	return NULL;
}

static struct _header_index *
edit_mail_header_create(struct edit_mail *edmail, const char *field_name)
{
	struct _header_index *header_idx;

	header_idx = edit_mail_header_find(edmail, field_name);
	if (header_idx == NULL) {
		header_idx = i_new(struct _header_index, 1);
		header_idx->header = _header_create(field_name);

		DLLIST2_APPEND(&edmail->headers_head, &edmail->headers_tail,
			       header_idx);
	}

	return header_idx;
}

static struct _header_index *
edit_mail_header_clone(struct edit_mail *edmail, struct _header *header)
{
	struct _header_index *header_idx;

	header_idx = edmail->headers_head;
	while (header_idx != NULL) {
		if (header_idx->header == header)
			return header_idx;

		header_idx = header_idx->next;
	}

	header_idx = i_new(struct _header_index, 1);
	header_idx->header = header;
	_header_ref(header);
	DLLIST2_APPEND(&edmail->headers_head, &edmail->headers_tail,
		       header_idx);

	return header_idx;
}

static struct _header_field_index *
edit_mail_header_field_create(struct edit_mail *edmail, const char *field_name,
			      const char *value)
{
	struct _header_index *header_idx;
	struct _header *header;
	struct _header_field_index *field_idx;
	struct _header_field *field;
	unsigned int lines;

	/* Get/create header index item */
	header_idx = edit_mail_header_create(edmail, field_name);
	header = header_idx->header;

	/* Create new field index item */
	field_idx = i_new(struct _header_field_index, 1);
	field_idx->header = header_idx;
	field_idx->field = field = _header_field_create(header);

	/* Create header field data (folded if necessary) */
	T_BEGIN {
		string_t *enc_value, *data;

		enc_value = t_str_new(strlen(field_name) + strlen(value) + 64);
		data = t_str_new(strlen(field_name) + strlen(value) + 128);

		message_header_encode(value, enc_value);

		lines = rfc2822_header_append(data, field_name,
					      str_c(enc_value), edmail->crlf,
					      &field->body_offset);

		/* Copy to new field */
		field->data = i_strndup(str_data(data), str_len(data));
		field->size = str_len(data);
		field->virtual_size = (edmail->crlf ?
				       field->size : field->size + lines);
		field->lines = lines;
	} T_END;

	/* Record original (utf8) value */
	field->utf8_value = _header_value_unfold(value);

	return field_idx;
}

static void
edit_mail_header_field_delete(struct edit_mail *edmail,
			      struct _header_field_index *field_idx,
			      bool update_index)
{
	struct _header_index *header_idx = field_idx->header;
	struct _header_field *field = field_idx->field;

	i_assert(header_idx != NULL);

	edmail->hdr_size.physical_size -= field->size;
	edmail->hdr_size.virtual_size -= field->virtual_size;
	edmail->hdr_size.lines -= field->lines;

	header_idx->count--;
	if (update_index) {
		if (header_idx->count == 0) {
			DLLIST2_REMOVE(&edmail->headers_head,
				       &edmail->headers_tail, header_idx);
			_header_unref(header_idx->header);
			i_free(header_idx);
		} else if (header_idx->first == field_idx) {
			struct _header_field_index *hfield =
				header_idx->first->next;

			while (hfield != NULL && hfield->header != header_idx)
				hfield = hfield->next;

			i_assert(hfield != NULL);
			header_idx->first = hfield;
		} else if (header_idx->last == field_idx) {
			struct _header_field_index *hfield =
				header_idx->last->prev;

			while (hfield != NULL && hfield->header != header_idx)
				hfield = hfield->prev;

			i_assert(hfield != NULL);
			header_idx->last = hfield;
		}
	}

	DLLIST2_REMOVE(&edmail->header_fields_head, &edmail->header_fields_tail,
		       field_idx);
	_header_field_unref(field_idx->field);
	i_free(field_idx);
}

static struct _header_field_index *
edit_mail_header_field_replace(struct edit_mail *edmail,
			       struct _header_field_index *field_idx,
			       const char *newname, const char *newvalue,
			       bool update_index)
{
	struct _header_field_index *field_idx_new;
	struct _header_index *header_idx = field_idx->header, *header_idx_new;
	struct _header_field *field = field_idx->field, *field_new;

	i_assert(header_idx != NULL);
	i_assert(newname != NULL || newvalue != NULL);

	if (newname == NULL)
		newname = header_idx->header->name;
	if (newvalue == NULL)
		newvalue = field_idx->field->utf8_value;
	field_idx_new = edit_mail_header_field_create(
		edmail, newname, newvalue);
	field_new = field_idx_new->field;
	header_idx_new = field_idx_new->header;

	edmail->hdr_size.physical_size -= field->size;
	edmail->hdr_size.virtual_size -= field->virtual_size;
	edmail->hdr_size.lines -= field->lines;

	edmail->hdr_size.physical_size += field_new->size;
	edmail->hdr_size.virtual_size += field_new->virtual_size;
	edmail->hdr_size.lines += field_new->lines;

	/* Replace header field index */
	field_idx_new->prev = field_idx->prev;
	field_idx_new->next = field_idx->next;
	if (field_idx->prev != NULL)
		field_idx->prev->next = field_idx_new;
	if (field_idx->next != NULL)
		field_idx->next->prev = field_idx_new;
	if (edmail->header_fields_head == field_idx)
		edmail->header_fields_head = field_idx_new;
	if (edmail->header_fields_tail == field_idx)
		edmail->header_fields_tail = field_idx_new;

	if (header_idx_new == header_idx) {
		if (header_idx->first == field_idx)
			header_idx->first = field_idx_new;
		if (header_idx->last == field_idx)
			header_idx->last = field_idx_new;
	} else {
		header_idx->count--;
		header_idx_new->count++;

		if (update_index) {
			if (header_idx->count == 0) {
				DLLIST2_REMOVE(&edmail->headers_head,
					       &edmail->headers_tail,
					       header_idx);
				_header_unref(header_idx->header);
				i_free(header_idx);
			} else if (header_idx->first == field_idx) {
				struct _header_field_index *hfield =
					header_idx->first->next;

				while (hfield != NULL &&
				       hfield->header != header_idx)
					hfield = hfield->next;

				i_assert(hfield != NULL);
				header_idx->first = hfield;
			} else if (header_idx->last == field_idx) {
				struct _header_field_index *hfield =
					header_idx->last->prev;

				while (hfield != NULL &&
				       hfield->header != header_idx)
					hfield = hfield->prev;

				i_assert(hfield != NULL);
				header_idx->last = hfield;
			}
			if (header_idx_new->count > 0) {
				struct _header_field_index *hfield;

				hfield = edmail->header_fields_head;
				while (hfield != NULL &&
				       hfield->header != header_idx_new)
					hfield = hfield->next;

				i_assert(hfield != NULL);
				header_idx_new->first = hfield;

				hfield = edmail->header_fields_tail;
				while (hfield != NULL &&
				       hfield->header != header_idx_new)
					hfield = hfield->prev;

				i_assert(hfield != NULL);
				header_idx_new->last = hfield;
			}
		}
	}

	_header_field_unref(field_idx->field);
	i_free(field_idx);
	return field_idx_new;
}

static inline char *
_header_decode(const unsigned char *hdr_data, size_t hdr_data_len)
{
	string_t *str = t_str_new(512);

	/* hdr_data is already unfolded */

	/* Decode MIME encoded-words. */
	message_header_decode_utf8((const unsigned char *)hdr_data,
				   hdr_data_len, str, NULL);
	return i_strdup(str_c(str));
}

static int edit_mail_headers_parse(struct edit_mail *edmail)
{
	struct message_header_parser_ctx *hparser;
	enum message_header_parser_flags hparser_flags =
		MESSAGE_HEADER_PARSER_FLAG_SKIP_INITIAL_LWSP |
		MESSAGE_HEADER_PARSER_FLAG_CLEAN_ONELINE;
	struct message_header_line *hdr;
	struct _header_index *header_idx;
	struct _header_field_index *head = NULL, *tail = NULL, *current;
	string_t *hdr_data;
	uoff_t offset = 0, body_offset = 0, vsize_diff = 0;
	unsigned int lines = 0;
	int ret;

	if (edmail->headers_parsed)
		return 1;

	i_stream_seek(edmail->wrapped_stream, 0);
	hparser = message_parse_header_init(edmail->wrapped_stream, NULL,
					    hparser_flags);

	T_BEGIN {
		hdr_data = t_str_new(1024);
		while ((ret = message_parse_header_next(hparser, &hdr)) > 0) {
			struct _header_field_index *field_idx_new;
			struct _header_field *field;

			if (hdr->eoh) {
				/* Record whether header ends in CRLF or LF */
				edmail->eoh_crlf = hdr->crlf_newline;
			}

			if (hdr == NULL || hdr->eoh)
				break;

			/* Skip bad headers */
			if (hdr->name_len == 0)
				continue;
			/* We deny the existence of any 'Content-Length:'
			   header. This header is non-standard and it can wreak
			   havok when the message is modified.
			 */
			if (strcasecmp(hdr->name, "Content-Length" ) == 0)
				continue;

			if (hdr->continued) {
				/* Continued line of folded header */
				buffer_append(hdr_data, hdr->value,
					      hdr->value_len);
			} else {
				/* First line of header */
				offset = hdr->name_offset;
				body_offset = hdr->name_len + hdr->middle_len;
				str_truncate(hdr_data, 0);
				buffer_append(hdr_data, hdr->name,
					      hdr->name_len);
				buffer_append(hdr_data, hdr->middle,
					      hdr->middle_len);
				buffer_append(hdr_data, hdr->value,
					      hdr->value_len);
				lines = 0;
				vsize_diff = 0;
			}

			if (!hdr->no_newline) {
				lines++;

				if (hdr->crlf_newline) {
					buffer_append(hdr_data, "\r\n", 2);
				} else {
					buffer_append(hdr_data, "\n", 1);
					vsize_diff++;
				}
			}

			if (hdr->continues) {
				hdr->use_full_value = TRUE;
				continue;
			}

			/* Create new header field index entry */

			field_idx_new = i_new(struct _header_field_index, 1);

			header_idx = edit_mail_header_create(edmail, hdr->name);
			header_idx->count++;
			field_idx_new->header = header_idx;
			field_idx_new->field = field =
				_header_field_create(header_idx->header);

			i_assert(body_offset > 0);
			field->body_offset = body_offset;

			field->utf8_value = _header_decode(hdr->full_value,
							   hdr->full_value_len);

			field->size = str_len(hdr_data);
			field->virtual_size = field->size + vsize_diff;
			field->data = i_strndup(str_data(hdr_data),
						field->size);
			field->offset = offset;
			field->lines = lines;

			DLLIST2_APPEND(&head, &tail, field_idx_new);

			edmail->hdr_size.physical_size += field->size;
			edmail->hdr_size.virtual_size += field->virtual_size;
			edmail->hdr_size.lines += lines;
		}
	} T_END;

	message_parse_header_deinit(&hparser);

	/* Blocking i/o required */
	i_assert(ret != 0);

	if (ret < 0 && edmail->wrapped_stream->stream_errno != 0) {
		/* Error; clean up */
		i_error("read(%s) failed: %s",
			i_stream_get_name(edmail->wrapped_stream),
			i_stream_get_error(edmail->wrapped_stream));
		current = head;
		while (current != NULL) {
			struct _header_field_index *next = current->next;

			_header_field_unref(current->field);
			i_free(current);

			current = next;
		}

		return ret;
	}

	/* Insert header field index items in main list */
	if (head != NULL && tail != NULL) {
		if (edmail->header_fields_appended != NULL) {
			if (edmail->header_fields_head !=
			    edmail->header_fields_appended) {
				edmail->header_fields_appended->prev->next = head;
				head->prev = edmail->header_fields_appended->prev;
			} else {
				edmail->header_fields_head = head;
			}

			tail->next = edmail->header_fields_appended;
			edmail->header_fields_appended->prev = tail;
		} else if (edmail->header_fields_tail != NULL) {
			edmail->header_fields_tail->next = head;
			head->prev = edmail->header_fields_tail;
			edmail->header_fields_tail = tail;
		} else {
			edmail->header_fields_head = head;
			edmail->header_fields_tail = tail;
		}
	}

	/* Rebuild header index */
	current = edmail->header_fields_head;
	while (current != NULL) {
		if (current->header->first == NULL)
			current->header->first = current;
		current->header->last = current;

		current = current->next;
	}

	/* Clear appended headers */
	edmail->header_fields_appended = NULL;
	edmail->appended_hdr_size.physical_size = 0;
	edmail->appended_hdr_size.virtual_size = 0;
	edmail->appended_hdr_size.lines = 0;

	/* Do not parse headers again */
	edmail->headers_parsed = TRUE;

	return 1;
}

void edit_mail_header_add(struct edit_mail *edmail, const char *field_name,
			  const char *value, bool last)
{
	struct _header_index *header_idx;
	struct _header_field_index *field_idx;
	struct _header_field *field;

	edit_mail_modify(edmail);

	field_idx = edit_mail_header_field_create(edmail, field_name, value);
	header_idx = field_idx->header;
	field = field_idx->field;

	/* Add it to the header field index */
	if (last) {
		DLLIST2_APPEND(&edmail->header_fields_head,
			       &edmail->header_fields_tail, field_idx);

		header_idx->last = field_idx;
		if (header_idx->first == NULL)
			header_idx->first = field_idx;

		if (!edmail->headers_parsed)  {
			if (edmail->header_fields_appended == NULL) {
				/* Record beginning of appended headers */
				edmail->header_fields_appended = field_idx;
			}

			edmail->appended_hdr_size.physical_size += field->size;
			edmail->appended_hdr_size.virtual_size += field->virtual_size;
			edmail->appended_hdr_size.lines += field->lines;
		}
	} else {
		DLLIST2_PREPEND(&edmail->header_fields_head,
				&edmail->header_fields_tail, field_idx);

		header_idx->first = field_idx;
		if (header_idx->last == NULL)
			header_idx->last = field_idx;
	}

	header_idx->count++;

	edmail->hdr_size.physical_size += field->size;
	edmail->hdr_size.virtual_size += field->virtual_size;
	edmail->hdr_size.lines += field->lines;
}

int edit_mail_header_delete(struct edit_mail *edmail, const char *field_name,
			    int index)
{
	struct _header_index *header_idx;
	struct _header_field_index *field_idx;
	int pos = 0;
	int ret = 0;

	/* Make sure headers are parsed */
	if (edit_mail_headers_parse(edmail) <= 0)
		return -1;

	/* Find the header entry */
	header_idx = edit_mail_header_find(edmail, field_name);
	if (header_idx == NULL) {
		/* Not found */
		return 0;
	}

	/* Signal modification */
	edit_mail_modify(edmail);

	/* Iterate through all header fields and remove those that match */
	field_idx = (index >= 0 ? header_idx->first : header_idx->last);
	while (field_idx != NULL) {
		struct _header_field_index *next =
			(index >= 0 ? field_idx->next : field_idx->prev);

		if (field_idx->field->header == header_idx->header) {
			bool final;

			if (index >= 0) {
				pos++;
				final = (header_idx->last == field_idx);
			} else {
				pos--;
				final = (header_idx->first == field_idx);
			}

			if (index == 0 || index == pos) {
				if (header_idx->first == field_idx)
					header_idx->first = NULL;
				if (header_idx->last == field_idx)
					header_idx->last = NULL;
				edit_mail_header_field_delete(
					edmail, field_idx, FALSE);
				ret++;
			}

			if (final || (index != 0 && index == pos))
				break;
		}

		field_idx = next;
	}

	if (index == 0 || header_idx->count == 0) {
		DLLIST2_REMOVE(&edmail->headers_head,
			       &edmail->headers_tail, header_idx);
		_header_unref(header_idx->header);
		i_free(header_idx);
	} else if (header_idx->first == NULL || header_idx->last == NULL) {
		struct _header_field_index *current =
			edmail->header_fields_head;

		while (current != NULL) {
			if (current->header == header_idx) {
				if (header_idx->first == NULL)
					header_idx->first = current;
				header_idx->last = current;
			}
			current = current->next;
		}
	}

	return ret;
}

int edit_mail_header_replace(struct edit_mail *edmail,
			     const char *field_name, int index,
			     const char *newname, const char *newvalue)
{
	struct _header_index *header_idx, *header_idx_new;
	struct _header_field_index *field_idx, *field_idx_new;
	int pos = 0;
	int ret = 0;

	/* Make sure headers are parsed */
	if (edit_mail_headers_parse(edmail) <= 0)
		return -1;

	/* Find the header entry */
	header_idx = edit_mail_header_find(edmail, field_name);
	if (header_idx == NULL) {
		/* Not found */
		return 0;
	}

	/* Signal modification */
	edit_mail_modify(edmail);

	/* Iterate through all header fields and replace those that match */
	field_idx = (index >= 0 ? header_idx->first : header_idx->last);
	field_idx_new = NULL;
	while (field_idx != NULL) {
		struct _header_field_index *next =
			(index >= 0 ? field_idx->next : field_idx->prev);

		if (field_idx->field->header == header_idx->header) {
			bool final;

			if (index >= 0) {
				pos++;
				final = (header_idx->last == field_idx);
			} else {
				pos--;
				final = (header_idx->first == field_idx);
			}

			if (index == 0 || index == pos) {
				if (header_idx->first == field_idx)
					header_idx->first = NULL;
				if (header_idx->last == field_idx)
					header_idx->last = NULL;
				field_idx_new = edit_mail_header_field_replace(
					edmail, field_idx, newname, newvalue,
					FALSE);
				ret++;
			}

			if (final || (index != 0 && index == pos))
				break;
		}

		field_idx = next;
	}

	/* Update old header index */
	if (header_idx->count == 0) {
		DLLIST2_REMOVE(&edmail->headers_head, &edmail->headers_tail,
			       header_idx);
		_header_unref(header_idx->header);
		i_free(header_idx);
	} else if (header_idx->first == NULL || header_idx->last == NULL) {
		struct _header_field_index *current =
			edmail->header_fields_head;

		while (current != NULL) {
			if (current->header == header_idx) {
				if (header_idx->first == NULL)
					header_idx->first = current;
				header_idx->last = current;
			}
			current = current->next;
		}
	}

	/* Update new header index */
	if (field_idx_new != NULL) {
		struct _header_field_index *current =
			edmail->header_fields_head;

		header_idx_new = field_idx_new->header;
		while (current != NULL) {
			if (current->header == header_idx_new) {
				if (header_idx_new->first == NULL)
					header_idx_new->first = current;
				header_idx_new->last = current;
			}
			current = current->next;
		}
	}

	return ret;
}

struct edit_mail_header_iter
{
	struct edit_mail *mail;
	struct _header_index *header;
	struct _header_field_index *current;

	bool reverse:1;
};

int edit_mail_headers_iterate_init(struct edit_mail *edmail,
				   const char *field_name, bool reverse,
				   struct edit_mail_header_iter **edhiter_r)
{
	struct edit_mail_header_iter *edhiter;
	struct _header_index *header_idx = NULL;
	struct _header_field_index *current = NULL;

	/* Make sure headers are parsed */
	if (edit_mail_headers_parse(edmail) <= 0) {
		/* Failure */
		return -1;
	}

	header_idx = edit_mail_header_find(edmail, field_name);

	if (field_name != NULL && header_idx == NULL) {
		current = NULL;
	} else if (!reverse) {
		current = (header_idx != NULL ?
			   header_idx->first : edmail->header_fields_head);
	} else {
		current = (header_idx != NULL ?
			   header_idx->last : edmail->header_fields_tail);
		if (current->header == NULL)
			current = current->prev;
	}

	if (current ==  NULL)
		return 0;

 	edhiter = i_new(struct edit_mail_header_iter, 1);
	edhiter->mail = edmail;
	edhiter->header = header_idx;
	edhiter->reverse = reverse;
	edhiter->current = current;

	*edhiter_r = edhiter;
	return 1;
}

void edit_mail_headers_iterate_deinit(struct edit_mail_header_iter **edhiter)
{
	i_free(*edhiter);
	*edhiter = NULL;
}

void edit_mail_headers_iterate_get(struct edit_mail_header_iter *edhiter,
				   const char **value_r)
{
	const char *raw;
	int i;

	i_assert(edhiter->current != NULL && edhiter->current->header != NULL);

	raw = edhiter->current->field->utf8_value;
	for (i = strlen(raw)-1; i >= 0; i--) {
		if (raw[i] != ' ' && raw[i] != '\t')
			break;
	}

	*value_r = t_strndup(raw, i+1);
}

bool edit_mail_headers_iterate_next(struct edit_mail_header_iter *edhiter)
{
	if (edhiter->current == NULL)
		return FALSE;

	do {
		edhiter->current = (!edhiter->reverse ?
				    edhiter->current->next :
				    edhiter->current->prev );
	} while (edhiter->current != NULL && edhiter->current->header != NULL &&
		 edhiter->header != NULL &&
		 edhiter->current->header != edhiter->header);

	return (edhiter->current != NULL && edhiter->current->header != NULL);
}

bool edit_mail_headers_iterate_remove(struct edit_mail_header_iter *edhiter)
{
	struct _header_field_index *field_idx;
	bool next;

	i_assert(edhiter->current != NULL && edhiter->current->header != NULL);

	edit_mail_modify(edhiter->mail);

	field_idx = edhiter->current;
	next = edit_mail_headers_iterate_next(edhiter);
	edit_mail_header_field_delete(edhiter->mail, field_idx, TRUE);

	return next;
}

bool edit_mail_headers_iterate_replace(struct edit_mail_header_iter *edhiter,
				       const char *newname,
				       const char *newvalue)
{
	struct _header_field_index *field_idx;
	bool next;

	i_assert(edhiter->current != NULL && edhiter->current->header != NULL);

	edit_mail_modify(edhiter->mail);

	field_idx = edhiter->current;
	next = edit_mail_headers_iterate_next(edhiter);
	edit_mail_header_field_replace(edhiter->mail, field_idx,
				       newname, newvalue, TRUE);

	return next;
}

/* Body modification */

// FIXME: implement

/*
 * Mail API
 */

static void edit_mail_close(struct mail *mail)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	edmail->wrapped->v.close(&edmail->wrapped->mail);
}

static void edit_mail_free(struct mail *mail)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	edmail->wrapped->v.free(&edmail->wrapped->mail);

	edit_mail_unwrap(&edmail);
}

static void
edit_mail_set_seq(struct mail *mail ATTR_UNUSED, uint32_t seq ATTR_UNUSED,
		  bool saving ATTR_UNUSED)
{
	i_panic("edit_mail_set_seq() not implemented");
}

static bool ATTR_NORETURN
edit_mail_set_uid(struct mail *mail ATTR_UNUSED, uint32_t uid ATTR_UNUSED)
{
	i_panic("edit_mail_set_uid() not implemented");
}

static void edit_mail_set_uid_cache_updates(struct mail *mail, bool set)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	edmail->wrapped->v.set_uid_cache_updates(&edmail->wrapped->mail, set);
}

static void
edit_mail_add_temp_wanted_fields(
	struct mail *mail ATTR_UNUSED, enum mail_fetch_field fields ATTR_UNUSED,
	struct mailbox_header_lookup_ctx *headers ATTR_UNUSED)
{
  /* Nothing */
}

static enum mail_flags edit_mail_get_flags(struct mail *mail)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	return edmail->wrapped->v.get_flags(&edmail->wrapped->mail);
}

static const char *const *edit_mail_get_keywords(struct mail *mail)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	return edmail->wrapped->v.get_keywords(&edmail->wrapped->mail);
}

static const ARRAY_TYPE(keyword_indexes) *
edit_mail_get_keyword_indexes(struct mail *mail)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	return edmail->wrapped->v.get_keyword_indexes(&edmail->wrapped->mail);
}

static uint64_t edit_mail_get_modseq(struct mail *mail)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	return edmail->wrapped->v.get_modseq(&edmail->wrapped->mail);
}

static uint64_t edit_mail_get_pvt_modseq(struct mail *mail)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	return edmail->wrapped->v.get_pvt_modseq(&edmail->wrapped->mail);
}

static int edit_mail_get_parts(struct mail *mail, struct message_part **parts_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	return edmail->wrapped->v.get_parts(&edmail->wrapped->mail, parts_r);
}

static int
edit_mail_get_date(struct mail *mail, time_t *date_r, int *timezone_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	return edmail->wrapped->v.get_date(&edmail->wrapped->mail,
					   date_r, timezone_r);
}

static int edit_mail_get_received_date(struct mail *mail, time_t *date_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	return edmail->wrapped->v.get_received_date(&edmail->wrapped->mail,
						    date_r);
}

static int edit_mail_get_save_date(struct mail *mail, time_t *date_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	return edmail->wrapped->v.get_save_date(&edmail->wrapped->mail, date_r);
}

static int edit_mail_get_virtual_size(struct mail *mail, uoff_t *size_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	if (!edmail->headers_parsed) {
		*size_r = (edmail->wrapped_hdr_size.virtual_size +
			   edmail->wrapped_body_size.virtual_size);

		if (!edmail->modified)
			return 0;
	} else {
		*size_r = edmail->wrapped_body_size.virtual_size + 2;
	}

	*size_r += (edmail->hdr_size.virtual_size +
		    edmail->body_size.virtual_size);
	return 0;
}

static int edit_mail_get_physical_size(struct mail *mail, uoff_t *size_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	*size_r = 0;
	if (!edmail->headers_parsed) {
		*size_r = (edmail->wrapped_hdr_size.physical_size +
			   edmail->wrapped_body_size.physical_size);

		if (!edmail->modified)
			return 0;
	} else {
		*size_r = (edmail->wrapped_body_size.physical_size +
			   (edmail->eoh_crlf ? 2 : 1));
	}

	*size_r += (edmail->hdr_size.physical_size +
		    edmail->body_size.physical_size);
	return 0;
}

static int
edit_mail_get_first_header(struct mail *mail, const char *field_name,
			   bool decode_to_utf8, const char **value_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;
	struct _header_index *header_idx;
	struct _header_field *field;
	int ret;

	/* Check whether mail headers were modified at all */
	if (!edmail->modified || edmail->headers_head == NULL) {
		/* Unmodified */
		return edmail->wrapped->v.get_first_header(
			&edmail->wrapped->mail, field_name, decode_to_utf8,
			value_r);
	}

	/* Try to find modified header */
	header_idx = edit_mail_header_find(edmail, field_name);
	if (header_idx == NULL || header_idx->count == 0 ) {
		if (!edmail->headers_parsed) {
			/* No new header */
			return edmail->wrapped->v.get_first_header(
				&edmail->wrapped->mail, field_name,
				decode_to_utf8, value_r);
		}

		*value_r = NULL;
		return 0;
	}

	/* Get the first occurrence */
	if (edmail->header_fields_appended == NULL) {
		/* There are no appended headers, so first is found directly */
		field = header_idx->first->field;
	} else {
		struct _header_field_index *field_idx;

		/* Scan prepended headers */
		field_idx = edmail->header_fields_head;
		while (field_idx != NULL) {
			if (field_idx->header == header_idx)
				break;

			if (field_idx == edmail->header_fields_appended) {
				field_idx = NULL;
				break;
			}
			field_idx = field_idx->next;
		}

		if (field_idx == NULL) {
			/* Check original message */
			ret = edmail->wrapped->v.get_first_header(
				&edmail->wrapped->mail, field_name,
				decode_to_utf8, value_r);
			if (ret != 0)
				return ret;

			/* Use first (apparently appended) header */
			field = header_idx->first->field;
		} else {
			field = field_idx->field;
		}
	}

	if (decode_to_utf8)
		*value_r = field->utf8_value;
	else
		*value_r = (const char *)(field->data + field->body_offset);
	return 1;
}

static int
edit_mail_get_headers(struct mail *mail, const char *field_name,
		      bool decode_to_utf8, const char *const **value_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;
	struct _header_index *header_idx;
	struct _header_field_index *field_idx;
	const char *const *headers;
	ARRAY(const char *) header_values;

	if (!edmail->modified || edmail->headers_head == NULL) {
		/* Unmodified */
		return edmail->wrapped->v.get_headers(
			&edmail->wrapped->mail, field_name, decode_to_utf8,
			value_r);
	}

	header_idx = edit_mail_header_find(edmail, field_name);
	if (header_idx == NULL || header_idx->count == 0 ) {
		if (!edmail->headers_parsed) {
			/* No new header */
			return edmail->wrapped->v.get_headers(
				&edmail->wrapped->mail, field_name,
				decode_to_utf8, value_r);
		}

		p_array_init(&header_values, edmail->mail.pool, 1);
		(void)array_append_space(&header_values);
		*value_r = array_idx(&header_values, 0);
		return 0;
	}

	/* Merge */

	/* Read original headers too if message headers are not parsed */
	headers = NULL;
	if (!edmail->headers_parsed &&
	    edmail->wrapped->v.get_headers(&edmail->wrapped->mail, field_name,
					   decode_to_utf8, &headers) < 0)
		return -1;

	/* Fill result array */
	p_array_init(&header_values, edmail->mail.pool, 32);
	field_idx = header_idx->first;
	while (field_idx != NULL) {
		/* If current field is the first appended one, we need to add
		   original headers first.
		 */
		if (field_idx == edmail->header_fields_appended &&
		    headers != NULL) {
			while (*headers != NULL) {
				array_append(&header_values, headers, 1);
				headers++;
			}
		}

		/* Add modified header to the list */
		if (field_idx->field->header == header_idx->header) {
			struct _header_field *field = field_idx->field;

			const char *value;
			if (decode_to_utf8)
				value = field->utf8_value;
			else {
				value = (const char *)(field->data +
						       field->body_offset);
			}

			array_append(&header_values, &value, 1);

			if (field_idx == header_idx->last)
				break;
		}

		field_idx = field_idx->next;
	}

	/* Add original headers if necessary */
	if (headers != NULL) {
		while (*headers != NULL) {
			array_append(&header_values, headers, 1);
			headers++;
		}
	}

	(void)array_append_space(&header_values);
	*value_r = array_idx(&header_values, 0);
	return 1;
}

static int ATTR_NORETURN
edit_mail_get_header_stream(
	struct mail *mail ATTR_UNUSED,
	struct mailbox_header_lookup_ctx *headers ATTR_UNUSED,
	struct istream **stream_r ATTR_UNUSED)
{
	// FIXME: implement!
	i_panic("edit_mail_get_header_stream() not implemented");
}

static int
edit_mail_get_stream(struct mail *mail, bool get_body ATTR_UNUSED,
		     struct message_size *hdr_size,
		     struct message_size *body_size, struct istream **stream_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	if (edmail->stream == NULL)
		edmail->stream = edit_mail_istream_create(edmail);

	if (hdr_size != NULL) {
		*hdr_size = edmail->wrapped_hdr_size;
		hdr_size->physical_size += edmail->hdr_size.physical_size;
		hdr_size->virtual_size += edmail->hdr_size.virtual_size;
		hdr_size->lines += edmail->hdr_size.lines;
	}

	if (body_size != NULL)
		*body_size = edmail->wrapped_body_size;

	*stream_r = edmail->stream;
	i_stream_seek(edmail->stream, 0);

	return 0;
}

static int
edit_mail_get_special(struct mail *mail, enum mail_fetch_field field,
		      const char **value_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	if (edmail->modified) {
		/* Block certain fields when modified */

		switch (field) {
		case MAIL_FETCH_GUID:
			/* This is in essence a new message */
			*value_r = "";
			return 0;
		case MAIL_FETCH_STORAGE_ID:
			/* Prevent hardlink copying */
			*value_r = "";
			return 0;
		default:
			break;
		}
	}

	return edmail->wrapped->v.get_special(&edmail->wrapped->mail,
					      field, value_r);
}

static int
edit_mail_get_backend_mail(struct mail *mail, struct mail **real_mail_r)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	*real_mail_r = edit_mail_get_mail(edmail);
	return 0;
}

static void
edit_mail_update_flags(struct mail *mail, enum modify_type modify_type,
		       enum mail_flags flags)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	edmail->wrapped->v.update_flags(&edmail->wrapped->mail,
					modify_type, flags);
}

static void
edit_mail_update_keywords(struct mail *mail, enum modify_type modify_type,
			  struct mail_keywords *keywords)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	edmail->wrapped->v.update_keywords(&edmail->wrapped->mail,
					   modify_type, keywords);
}

static void edit_mail_update_modseq(struct mail *mail, uint64_t min_modseq)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	edmail->wrapped->v.update_modseq(&edmail->wrapped->mail, min_modseq);
}

static void
edit_mail_update_pvt_modseq(struct mail *mail, uint64_t min_pvt_modseq)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	edmail->wrapped->v.update_pvt_modseq(&edmail->wrapped->mail,
					     min_pvt_modseq);
}

static void edit_mail_update_pop3_uidl(struct mail *mail, const char *uidl)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	if (edmail->wrapped->v.update_pop3_uidl != NULL) {
		edmail->wrapped->v.update_pop3_uidl(
			&edmail->wrapped->mail, uidl);
	}
}

static void edit_mail_expunge(struct mail *mail ATTR_UNUSED)
{
	/* NOOP */
}

static void
edit_mail_set_cache_corrupted(struct mail *mail, enum mail_fetch_field field,
			      const char *reason)
{
	struct edit_mail *edmail = (struct edit_mail *)mail;

	edmail->wrapped->v.set_cache_corrupted(&edmail->wrapped->mail,
					       field, reason);
}

static struct mail_vfuncs edit_mail_vfuncs = {
	edit_mail_close,
	edit_mail_free,
	edit_mail_set_seq,
	edit_mail_set_uid,
	edit_mail_set_uid_cache_updates,
	NULL,
	NULL,
	edit_mail_add_temp_wanted_fields,
	edit_mail_get_flags,
	edit_mail_get_keywords,
	edit_mail_get_keyword_indexes,
	edit_mail_get_modseq,
	edit_mail_get_pvt_modseq,
	edit_mail_get_parts,
	edit_mail_get_date,
	edit_mail_get_received_date,
	edit_mail_get_save_date,
	edit_mail_get_virtual_size,
	edit_mail_get_physical_size,
	edit_mail_get_first_header,
	edit_mail_get_headers,
	edit_mail_get_header_stream,
	edit_mail_get_stream,
	index_mail_get_binary_stream,
	edit_mail_get_special,
	edit_mail_get_backend_mail,
	edit_mail_update_flags,
	edit_mail_update_keywords,
	edit_mail_update_modseq,
	edit_mail_update_pvt_modseq,
	edit_mail_update_pop3_uidl,
	edit_mail_expunge,
	edit_mail_set_cache_corrupted,
	NULL,
};

/*
 * Edit Mail Stream
 */

struct edit_mail_istream {
	struct istream_private istream;
	pool_t pool;

	struct edit_mail *mail;

	struct _header_field_index *cur_header;
	uoff_t cur_header_v_offset;

	bool parent_buffer:1;
	bool header_read:1;
	bool eof:1;
};

static void edit_mail_istream_destroy(struct iostream_private *stream)
{
	struct edit_mail_istream *edstream =
		(struct edit_mail_istream *)stream;

	i_stream_unref(&edstream->istream.parent);
	i_stream_free_buffer(&edstream->istream);
	pool_unref(&edstream->pool);
}

static ssize_t
merge_from_parent(struct edit_mail_istream *edstream, uoff_t parent_v_offset,
		  uoff_t parent_end_v_offset, uoff_t copy_v_offset)
{
	struct istream_private *stream = &edstream->istream;
	uoff_t v_offset, append_v_offset;
	const unsigned char *data;
	size_t pos, cur_pos, parent_bytes_left;
	bool parent_buffer = edstream->parent_buffer;
	ssize_t ret;

	i_assert(parent_v_offset <= parent_end_v_offset);
	edstream->parent_buffer = FALSE;

	v_offset = stream->istream.v_offset;
	if (v_offset >= copy_v_offset) {
		i_assert((v_offset - copy_v_offset) <= parent_end_v_offset);
		if ((v_offset - copy_v_offset) == parent_end_v_offset) {
			/* Parent data is all read */
			return 0;
		}
	}

	/* Determine where we are appending more data to the stream */
	append_v_offset = v_offset + (stream->pos - stream->skip);

	if (v_offset >= copy_v_offset) {
		/* Parent buffer used */
		cur_pos = (stream->pos - stream->skip);
		parent_v_offset += (v_offset - copy_v_offset);
	} else {
		cur_pos = 0;
		i_assert(append_v_offset >= copy_v_offset);
		parent_v_offset += (append_v_offset - copy_v_offset);
	}

	/* Seek parent to required position */
	i_stream_seek(stream->parent, parent_v_offset);

	/* Read from parent */
	data = i_stream_get_data(stream->parent, &pos);
	if (pos > cur_pos)
		ret = 0;
	else do {
		/* Use normal read here, since parent data can be returned
		   directly to caller. */
		ret = i_stream_read(stream->parent);

		stream->istream.stream_errno = stream->parent->stream_errno;
		stream->istream.eof = stream->parent->eof;
		edstream->eof = stream->parent->eof;
		data = i_stream_get_data(stream->parent, &pos);
		/* Check again, in case the parent stream had been seeked
		   backwards and the previous read() didn't get us far
		   enough. */
	} while (pos <= cur_pos && ret > 0);

	/* Don't read beyond parent end offset */
	if (parent_end_v_offset != (uoff_t)-1) {
		parent_bytes_left = (size_t)(parent_end_v_offset -
					     parent_v_offset);
		if (pos >= parent_bytes_left) {
			pos = parent_bytes_left;
		}
	}

	if (v_offset < copy_v_offset || ret == -2 ||
	    (parent_buffer && (append_v_offset + 1) >= parent_end_v_offset)) {
		/* Merging with our local buffer; copying data from parent */
		if (pos > 0) {
			size_t avail;

			if (parent_buffer) {
				stream->pos = stream->skip = 0;
				stream->buffer = NULL;
			}
			if (!i_stream_try_alloc(stream, pos, &avail))
				return -2;
			pos = (pos > avail ? avail : pos);

			memcpy(stream->w_buffer + stream->pos, data, pos);
			stream->pos += pos;
			stream->buffer = stream->w_buffer;

			if (cur_pos >= pos)
				ret = 0;
			else
				ret = (ssize_t)(pos - cur_pos);
		} else {
			ret = (ret == 0 ? 0 : -1);
		}
	} else {
		/* Just passing buffers from parent; no copying */
		ret = (pos > cur_pos ?
		       (ssize_t)(pos - cur_pos) : (ret == 0 ? 0 : -1));
		stream->buffer = data;
		stream->pos = pos;
		stream->skip = 0;
		edstream->parent_buffer = TRUE;
	}

	i_assert(ret != -1 || stream->istream.eof ||
		 stream->istream.stream_errno != 0);
	return ret;
}

static ssize_t merge_modified_headers(struct edit_mail_istream *edstream)
{
	struct istream_private *stream = &edstream->istream;
	struct edit_mail *edmail = edstream->mail;
	uoff_t v_offset = stream->istream.v_offset, append_v_offset;
	size_t appended, written, avail, size;

	if (edstream->cur_header == NULL) {
		/* No (more) headers */
		return 0;
	}

	/* Caller must already have committed remaining parent data to
	   our stream buffer. */
	i_assert(!edstream->parent_buffer);

	/* Add modified headers to buffer */
	written = 0;
	while (edstream->cur_header != NULL) {
		size_t wsize;

		/* Determine what part of the header was already buffered */
		append_v_offset = v_offset + (stream->pos - stream->skip);
		i_assert(append_v_offset >= edstream->cur_header_v_offset);
		if (append_v_offset >= edstream->cur_header_v_offset)
			appended = (size_t)(append_v_offset -
					    edstream->cur_header_v_offset);
		else
			appended = 0;
		i_assert(appended <= edstream->cur_header->field->size);

		/* Determine how much we want to write */
		size = edstream->cur_header->field->size - appended;
		if (size > 0) {
			/* Determine how much we can write */
			if (!i_stream_try_alloc(stream, size, &avail)) {
				if (written == 0)
					return -2;
				break;
			}
			wsize = (size >= avail ? avail : size);

			/* Write (part of) the header to buffer */
			memcpy(stream->w_buffer + stream->pos,
			       edstream->cur_header->field->data + appended,
			       wsize);
			stream->pos += wsize;
			stream->buffer = stream->w_buffer;
			written += wsize;

			if (wsize < size) {
				/* Could not write whole header; finish here */
				break;
			}
		}

		/* Skip to next header */
		edstream->cur_header_v_offset +=
			edstream->cur_header->field->size;
		edstream->cur_header = edstream->cur_header->next;

		/* Stop at end of prepended headers if original header is left
		   unparsed */
		if (!edmail->headers_parsed &&
		    edstream->cur_header == edmail->header_fields_appended)
			edstream->cur_header = NULL;
	}

	if (edstream->cur_header == NULL) {
		/* Clear offset too, just to be tidy */
		edstream->cur_header_v_offset = 0;
	}

	i_assert(written > 0);
	return (ssize_t)written;
}

static ssize_t edit_mail_istream_read(struct istream_private *stream)
{
	struct edit_mail_istream *edstream =
		(struct edit_mail_istream *)stream;
	struct edit_mail *edmail = edstream->mail;
	uoff_t v_offset, append_v_offset;
	uoff_t parent_v_offset, parent_end_v_offset, copy_v_offset;
	uoff_t prep_hdr_size, hdr_size;
	ssize_t ret = 0;

	if (edstream->eof) {
		stream->istream.eof = TRUE;
		return -1;
	}

	if (edstream->parent_buffer && stream->skip == stream->pos) {
		edstream->parent_buffer = FALSE;
		stream->pos = stream->skip = 0;
		stream->buffer = NULL;
	}

	/* Merge prepended headers */
	if (!edstream->parent_buffer) {
		ret = merge_modified_headers(edstream);
		if (ret != 0)
			return ret;
	}
	v_offset = stream->istream.v_offset;
	append_v_offset = v_offset + (stream->pos - stream->skip);

	if (!edmail->headers_parsed && edmail->header_fields_appended != NULL &&
	    !edstream->header_read) {
		/* Output headers from original stream */

		/* Size of the prepended header */
		i_assert(edmail->hdr_size.physical_size >=
			 edmail->appended_hdr_size.physical_size);
		prep_hdr_size = (edmail->hdr_size.physical_size -
				 edmail->appended_hdr_size.physical_size);

		/* Calculate offset of header end or appended header. Any final
		   CR is dealt with later.
		 */
		hdr_size = (prep_hdr_size +
			    edmail->wrapped_hdr_size.physical_size);
		if (hdr_size == 0) {
			/* Corner case that doesn't happen in practice (the
			   original message is never empty). */
			edstream->cur_header = edmail->header_fields_appended;
			edstream->cur_header_v_offset = v_offset;
			edstream->header_read = TRUE;
		} else if (append_v_offset <= (hdr_size - 1) &&
			   edmail->wrapped_hdr_size.physical_size > 0) {
			parent_v_offset = stream->parent_start_offset;
			parent_end_v_offset =
				(stream->parent_start_offset +
				 edmail->wrapped_hdr_size.physical_size - 1);
			copy_v_offset = prep_hdr_size;

			ret = merge_from_parent(edstream, parent_v_offset,
						parent_end_v_offset,
						copy_v_offset);
			if (ret < 0)
				return ret;
			append_v_offset = (v_offset +
					   (stream->pos - stream->skip));
			i_assert(append_v_offset <= hdr_size - 1);

			if (append_v_offset == hdr_size - 1) {
				/* Strip final CR too when it is present */
				if (stream->buffer != NULL &&
				    stream->buffer[stream->pos-1] == '\r') {
					stream->pos--;
					append_v_offset--;
					ret--;
				}

				i_assert(ret >= 0);
				edstream->cur_header =
					edmail->header_fields_appended;
				edstream->cur_header_v_offset = append_v_offset;
				if (!edstream->parent_buffer)
					edstream->header_read = TRUE;
			}

			if (ret != 0)
				return ret;
		} else {
			edstream->header_read = TRUE;
		}

		/* Merge appended headers */
		ret = merge_modified_headers(edstream);
		if (ret != 0)
			return ret;
	}

	/* Header does not come from original mail at all */
	if (edmail->headers_parsed) {
		parent_v_offset = (stream->parent_start_offset +
				   edmail->wrapped_hdr_size.physical_size -
				   (edmail->eoh_crlf ? 2 : 1));
		copy_v_offset = edmail->hdr_size.physical_size;
	/* Corner case that doesn't happen in practice (the original message is
	   never empty). */
	} else if (edmail->wrapped_hdr_size.physical_size == 0) {
		parent_v_offset = stream->parent_start_offset;
		copy_v_offset = edmail->hdr_size.physical_size;
	/* Header comes partially from original mail and headers are added
	   between header and body. */
	} else if (edmail->header_fields_appended != NULL) {
		parent_v_offset = (stream->parent_start_offset +
				   edmail->wrapped_hdr_size.physical_size -
				   (edmail->eoh_crlf ? 2 : 1));
		copy_v_offset = (edmail->hdr_size.physical_size +
				 edmail->wrapped_hdr_size.physical_size -
				 (edmail->eoh_crlf ? 2 : 1));
	/* Header comes partially from original mail, but headers are only
	   prepended. */
	} else {
		parent_v_offset = stream->parent_start_offset;
		copy_v_offset = edmail->hdr_size.physical_size;
	}

	ret = merge_from_parent(edstream, parent_v_offset, (uoff_t)-1,
				copy_v_offset);
	if (ret != 0)
		return ret;

	stream->istream.eof = stream->parent->eof;
	edstream->eof = stream->parent->eof;
	return -1;
}

static void
stream_reset_to(struct edit_mail_istream *edstream, uoff_t v_offset)
{
	edstream->istream.istream.v_offset = v_offset;
	edstream->istream.skip = 0;
	edstream->istream.pos = 0;
	edstream->istream.buffer = NULL;
	edstream->parent_buffer = FALSE;
	edstream->eof = FALSE;
	i_stream_seek(edstream->istream.parent, 0);
}

static void
edit_mail_istream_seek(struct istream_private *stream, uoff_t v_offset,
		       bool mark ATTR_UNUSED)
{
	struct edit_mail_istream *edstream =
		(struct edit_mail_istream *)stream;
	struct _header_field_index *cur_header;
	struct edit_mail *edmail = edstream->mail;
	uoff_t offset;

	edstream->header_read = FALSE;
	edstream->cur_header = NULL;
	edstream->cur_header_v_offset = 0;

	/* The beginning */
	if (v_offset == 0) {
		stream_reset_to(edstream, 0);

		if (edmail->header_fields_head !=
		    edmail->header_fields_appended)
			edstream->cur_header = edmail->header_fields_head;
		return;
	}

	/* Inside (prepended) headers */
	if (edmail->headers_parsed) {
		offset = edmail->hdr_size.physical_size;
	} else {
		offset = (edmail->hdr_size.physical_size -
			  edmail->appended_hdr_size.physical_size);
	}

	if (v_offset < offset) {
		stream_reset_to(edstream, v_offset);

		/* Find the header */
		cur_header = edmail->header_fields_head;
		i_assert(cur_header != NULL &&
			 cur_header != edmail->header_fields_appended);
		edstream->cur_header_v_offset = 0;
		offset = cur_header->field->size;
		while (v_offset > offset) {
			cur_header = cur_header->next;
			i_assert(cur_header != NULL &&
				 cur_header != edmail->header_fields_appended);

			edstream->cur_header_v_offset = offset;
			offset += cur_header->field->size;
		}

		edstream->cur_header = cur_header;
		return;
	}

	if (!edmail->headers_parsed) {
		/* Inside original header */
		offset = (edmail->hdr_size.physical_size -
			  edmail->appended_hdr_size.physical_size +
			  edmail->wrapped_hdr_size.physical_size);
		if (v_offset < offset) {
			stream_reset_to(edstream, v_offset);
			return;
		}

		edstream->header_read = TRUE;

		/* Inside appended header */
		offset = (edmail->hdr_size.physical_size +
			  edmail->wrapped_hdr_size.physical_size);
		if (v_offset < offset) {
			stream_reset_to(edstream, v_offset);

			offset -= edmail->appended_hdr_size.physical_size;

			cur_header = edmail->header_fields_appended;
			i_assert(cur_header != NULL);
			edstream->cur_header_v_offset = offset;
			offset += cur_header->field->size;

			while (v_offset > offset) {
				cur_header = cur_header->next;
				i_assert(cur_header != NULL);

				edstream->cur_header_v_offset = offset;
				offset += cur_header->field->size;
			}

			edstream->cur_header = cur_header;
			return;
		}
	}

	stream_reset_to(edstream, v_offset);
	edstream->cur_header = NULL;
}

static void ATTR_NORETURN
edit_mail_istream_sync(struct istream_private *stream ATTR_UNUSED)
{
	i_panic("edit-mail istream sync() not implemented");
}

static int
edit_mail_istream_stat(struct istream_private *stream, bool exact)
{
	struct edit_mail_istream *edstream =
		(struct edit_mail_istream *)stream;
	struct edit_mail *edmail = edstream->mail;
	const struct stat *st;

	/* Stat the original stream */
	if (i_stream_stat(stream->parent, exact, &st) < 0)
		return -1;

	stream->statbuf = *st;
	if (st->st_size == -1 || !exact)
		return 0;

	if (!edmail->headers_parsed) {
		if (!edmail->modified)
			return 0;
	} else {
		stream->statbuf.st_size =
			(edmail->wrapped_body_size.physical_size +
			 (edmail->eoh_crlf ? 2 : 1));
	}

	stream->statbuf.st_size += (edmail->hdr_size.physical_size +
				    edmail->body_size.physical_size);
	return 0;
}

struct istream *edit_mail_istream_create(struct edit_mail *edmail)
{
	struct edit_mail_istream *edstream;
	struct istream *wrapped = edmail->wrapped_stream;

	edstream = i_new(struct edit_mail_istream, 1);
	edstream->pool = pool_alloconly_create(MEMPOOL_GROWING
					       "edit mail stream", 4096);
	edstream->mail = edmail;

	edstream->istream.max_buffer_size =
		wrapped->real_stream->max_buffer_size;

	edstream->istream.iostream.destroy = edit_mail_istream_destroy;
	edstream->istream.read = edit_mail_istream_read;
	edstream->istream.seek = edit_mail_istream_seek;
	edstream->istream.sync = edit_mail_istream_sync;
	edstream->istream.stat = edit_mail_istream_stat;

	edstream->istream.istream.readable_fd = FALSE;
	edstream->istream.istream.blocking = wrapped->blocking;
	edstream->istream.istream.seekable = wrapped->seekable;

	if (edmail->header_fields_head != edmail->header_fields_appended)
		edstream->cur_header = edmail->header_fields_head;

	i_stream_seek(wrapped, 0);

	return i_stream_create(&edstream->istream, wrapped, -1, 0);
}
