/* Copyright (c) 2002-2015 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_MESSAGE_H
#define __SIEVE_MESSAGE_H

#include "sieve-common.h"
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
	(struct sieve_message_context *msgctx);

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

struct sieve_stringlist *sieve_message_header_stringlist_create
	(const struct sieve_runtime_env *renv, struct sieve_stringlist *field_names,
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

#endif /* __SIEVE_MESSAGE_H */
