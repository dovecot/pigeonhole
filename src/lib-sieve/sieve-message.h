/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_MESSAGE_H
#define __SIEVE_MESSAGE_H

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-objects.h"

/*
 * Message transmission
 */

const char *sieve_message_get_new_id(const struct sieve_instance *svinst);

/*
 * Message context
 */

struct sieve_message_context;

struct sieve_message_context *sieve_message_context_create
	(struct sieve_instance *svinst, struct mail_user *mail_user,
		const struct sieve_message_data *msgdata);
void sieve_message_context_ref(struct sieve_message_context *msgctx);
void sieve_message_context_unref(struct sieve_message_context **msgctx);

void sieve_message_context_reset(struct sieve_message_context *msgctx);

pool_t sieve_message_context_pool
	(struct sieve_message_context *msgctx) ATTR_PURE;
void sieve_message_context_time(struct sieve_message_context *msgctx,
	struct timeval *time);

/* Extension support */

void sieve_message_context_extension_set
	(struct sieve_message_context *msgctx, const struct sieve_extension *ext,
		void *context);
const void *sieve_message_context_extension_get
	(struct sieve_message_context *msgctx, const struct sieve_extension *ext);

/* Envelope */

const struct sieve_address *sieve_message_get_final_recipient_address
	(struct sieve_message_context *msgctx);
const struct sieve_address *sieve_message_get_orig_recipient_address
	(struct sieve_message_context *msgctx);

const struct sieve_address *sieve_message_get_sender_address
	(struct sieve_message_context *msgctx);

const char *sieve_message_get_orig_recipient
	(struct sieve_message_context *msgctx);
const char *sieve_message_get_final_recipient
	(struct sieve_message_context *msgctx);

const char *sieve_message_get_sender
	(struct sieve_message_context *msgctx);

/* Mail */

struct mail *sieve_message_get_mail
	(struct sieve_message_context *msgctx);

int sieve_message_substitute
	(struct sieve_message_context *msgctx, struct istream *input);
struct edit_mail *sieve_message_edit
	(struct sieve_message_context *msgctx);
void sieve_message_snapshot
	(struct sieve_message_context *msgctx);

/*
 * Header stringlist
 */

struct sieve_header_list {
	struct sieve_stringlist strlist;

	int (*next_item)
		(struct sieve_header_list *_hdrlist, const char **name_r,
			string_t **value_r) ATTR_NULL(2);
};

static inline int sieve_header_list_next_item
(struct sieve_header_list *hdrlist, const char **name_r,
	string_t **value_r) ATTR_NULL(2)
{
	return hdrlist->next_item(hdrlist, name_r, value_r);
}

static inline void sieve_header_list_reset
(struct sieve_header_list *hdrlist)
{
	sieve_stringlist_reset(&hdrlist->strlist);
}

static inline int sieve_header_list_get_length
(struct sieve_header_list *hdrlist)
{
	return sieve_stringlist_get_length(&hdrlist->strlist);
}

static inline void sieve_header_list_set_trace
(struct sieve_header_list *hdrlist, bool trace)
{
	sieve_stringlist_set_trace(&hdrlist->strlist, trace);
}

struct sieve_header_list *sieve_message_header_list_create
	(const struct sieve_runtime_env *renv,
		struct sieve_stringlist *field_names,
		bool mime_decode);

/*
 * Message override
 */

/* Header override object */

struct sieve_message_override_def {
	struct sieve_object_def obj_def;

	unsigned int sequence;

	/* Context coding */

	bool (*dump_context)
		(const struct sieve_message_override *svmo,
			const struct sieve_dumptime_env *denv, sieve_size_t *address);
	int (*read_context)
		(const struct sieve_message_override *svmo,
			const struct sieve_runtime_env *renv, sieve_size_t *address,
			void **se_context);

	/* Override */

	int (*header_override)
		(const struct sieve_message_override *svmo,
			const struct sieve_runtime_env *renv,
			bool mime_decode, struct sieve_stringlist **headers);
};

struct sieve_message_override {
	struct sieve_object object;

	const struct sieve_message_override_def *def;

	void *context;
};

ARRAY_DEFINE_TYPE(sieve_message_override,
	struct sieve_message_override);

/*
 * Message override operand
 */

#define SIEVE_EXT_DEFINE_MESSAGE_OVERRIDE(SVMO) SIEVE_EXT_DEFINE_OBJECT(SVMO)
#define SIEVE_EXT_DEFINE_MESSAGE_OVERRIDES(SVMOS) SIEVE_EXT_DEFINE_OBJECTS(SMOS)

#define SIEVE_OPT_MESSAGE_OVERRIDE (-2)

extern const struct sieve_operand_class
	sieve_message_override_operand_class;

static inline void sieve_opr_message_override_emit
(struct sieve_binary_block *sblock, const struct sieve_extension *ext,
	const struct sieve_message_override_def *seff)
{
	sieve_opr_object_emit(sblock, ext, &seff->obj_def);
}

bool sieve_opr_message_override_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
int sieve_opr_message_override_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address,
		struct sieve_message_override *svmo);

/*
 * Optional operands
 */

int sieve_message_opr_optional_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address,
		signed int *opt_code);

int sieve_message_opr_optional_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address,
		signed int *opt_code, int *exec_status,
		struct sieve_address_part *addrp, struct sieve_match_type *mcht,
		struct sieve_comparator *cmp, 
		ARRAY_TYPE(sieve_message_override) *svmos);

/*
 * Message header
 */

int sieve_message_get_header_fields
	(const struct sieve_runtime_env *renv,
		struct sieve_stringlist *field_names,
		ARRAY_TYPE(sieve_message_override) *svmos,
		bool mime_decode, struct sieve_stringlist **fields_r);

/*
 * Message part
 */

struct sieve_message_part;

struct sieve_message_part_data {
	const char *content_type;
	const char *content_disposition;

	const char *content;
	unsigned long size;
};

struct sieve_message_part *sieve_message_part_parent
	(struct sieve_message_part *mpart) ATTR_PURE;
struct sieve_message_part *sieve_message_part_next
	(struct sieve_message_part *mpart) ATTR_PURE;
struct sieve_message_part *sieve_message_part_children
	(struct sieve_message_part *mpart) ATTR_PURE;

const char *sieve_message_part_content_type
	(struct sieve_message_part *mpart) ATTR_PURE;
const char *sieve_message_part_content_disposition
	(struct sieve_message_part *mpart) ATTR_PURE;

int sieve_message_part_get_first_header
	(struct sieve_message_part *mpart, const char *field,
		const char **value_r);

void sieve_message_part_get_data
	(struct sieve_message_part *mpart,
		struct sieve_message_part_data *data, bool text);

/*
 * Message body
 */

int sieve_message_body_get_content
	(const struct sieve_runtime_env *renv,
		const char * const *content_types,
		struct sieve_message_part_data **parts_r);
int sieve_message_body_get_text
	(const struct sieve_runtime_env *renv,
		struct sieve_message_part_data **parts_r);
int sieve_message_body_get_raw
	(const struct sieve_runtime_env *renv,
		struct sieve_message_part_data **parts_r);

/*
 * Message part iterator
 */

struct sieve_message_part_iter {
	const struct sieve_runtime_env *renv;
	struct sieve_message_part *root;
	unsigned int index, offset;
};

int sieve_message_part_iter_init
(struct sieve_message_part_iter *iter,
	const struct sieve_runtime_env *renv);
void sieve_message_part_iter_subtree(struct sieve_message_part_iter *iter,
	struct sieve_message_part_iter *subtree);
void sieve_message_part_iter_children(struct sieve_message_part_iter *iter,
	struct sieve_message_part_iter *child);

struct sieve_message_part *sieve_message_part_iter_current
(struct sieve_message_part_iter *iter);
struct sieve_message_part *sieve_message_part_iter_next
(struct sieve_message_part_iter *iter);

void sieve_message_part_iter_reset
(struct sieve_message_part_iter *iter);

/*
 * MIME header list
 */

struct sieve_header_list *sieve_mime_header_list_create
(const struct sieve_runtime_env *renv,
	struct sieve_stringlist *field_names,
	struct sieve_message_part_iter *part_iter,
	bool mime_decode, bool children);

#endif /* __SIEVE_MESSAGE_H */
