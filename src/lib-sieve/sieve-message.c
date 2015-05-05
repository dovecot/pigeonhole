/* Copyright (c) 2002-2015 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "ioloop.h"
#include "mempool.h"
#include "array.h"
#include "str.h"
#include "str-sanitize.h"
#include "istream.h"
#include "mail-storage.h"
#include "mail-user.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "raw-storage.h"

#include "edit-mail.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-address-parts.h"
#include "sieve-runtime.h"
#include "sieve-runtime-trace.h"
#include "sieve-match.h"
#include "sieve-interpreter.h"
#include "sieve-address.h"

#include "sieve-message.h"

/*
 * Message transmission
 */

const char *sieve_message_get_new_id(const struct sieve_instance *svinst)
{
	static int count = 0;

	return t_strdup_printf("<dovecot-sieve-%s-%s-%d@%s>",
		dec2str(ioloop_timeval.tv_sec), dec2str(ioloop_timeval.tv_usec),
    count++, svinst->hostname);
}

/*
 * Message context
 */

struct sieve_message_version {
	struct mail *mail;
	struct mailbox *box;
	struct mailbox_transaction_context *trans;
	struct edit_mail *edit_mail;
};

struct sieve_message_context {
	pool_t pool;
	pool_t context_pool;
	int refcount;

	struct sieve_instance *svinst;

	struct mail_user *mail_user;
	const struct sieve_message_data *msgdata;

	/* Normalized envelope addresses */

	bool envelope_parsed;

	const struct sieve_address *envelope_sender;
	const struct sieve_address *envelope_orig_recipient;
	const struct sieve_address *envelope_final_recipient;

	/* Message versioning */

	struct mail_user *raw_mail_user;
	ARRAY(struct sieve_message_version) versions;

	/* Context data for extensions */
	ARRAY(void *) ext_contexts;

	unsigned int edit_snapshot:1;
	unsigned int substitute_snapshot:1;
};

/*
 * Message versions
 */

static inline struct sieve_message_version *sieve_message_version_new
(struct sieve_message_context *msgctx)
{
	return array_append_space(&msgctx->versions);
}

static inline struct sieve_message_version *sieve_message_version_get
(struct sieve_message_context *msgctx)
{
	struct sieve_message_version *versions;
	unsigned int count;

	versions = array_get_modifiable(&msgctx->versions, &count);
	if ( count == 0 )
		return array_append_space(&msgctx->versions);

	return &versions[count-1];
}

static inline void sieve_message_version_free
(struct sieve_message_version *version)
{
	if ( version->edit_mail != NULL ) {
		edit_mail_unwrap(&version->edit_mail);
		version->edit_mail = NULL;
	}

	if ( version->mail != NULL ) {
		mail_free(&version->mail);
		mailbox_transaction_rollback(&version->trans);
		mailbox_free(&version->box);
		version->mail = NULL;
	}
}

/*
 * Message context object
 */

struct sieve_message_context *sieve_message_context_create
(struct sieve_instance *svinst, struct mail_user *mail_user,
	const struct sieve_message_data *msgdata)
{
	struct sieve_message_context *msgctx;

	msgctx = i_new(struct sieve_message_context, 1);
	msgctx->refcount = 1;
	msgctx->svinst = svinst;

	msgctx->mail_user = mail_user;
	msgctx->msgdata = msgdata;

	sieve_message_context_reset(msgctx);

	return msgctx;
}

void sieve_message_context_ref(struct sieve_message_context *msgctx)
{
	msgctx->refcount++;
}

static void sieve_message_context_clear(struct sieve_message_context *msgctx)
{
	struct sieve_message_version *versions;
	unsigned int count, i;

	if ( msgctx->pool != NULL ) {
		versions = array_get_modifiable(&msgctx->versions, &count);

		for ( i = 0; i < count; i++ ) {
			sieve_message_version_free(&versions[i]);
		}

		pool_unref(&(msgctx->pool));
	}

	msgctx->envelope_orig_recipient = NULL;
	msgctx->envelope_final_recipient = NULL;
	msgctx->envelope_sender = NULL;
	msgctx->envelope_parsed = FALSE;
}

void sieve_message_context_unref(struct sieve_message_context **msgctx)
{
	i_assert((*msgctx)->refcount > 0);

	if (--(*msgctx)->refcount != 0)
		return;

	if ( (*msgctx)->raw_mail_user != NULL )
		mail_user_unref(&(*msgctx)->raw_mail_user);

	sieve_message_context_clear(*msgctx);

	if ( (*msgctx)->context_pool != NULL )
		pool_unref(&((*msgctx)->context_pool));

	i_free(*msgctx);
	*msgctx = NULL;
}

static void sieve_message_context_flush(struct sieve_message_context *msgctx)
{
	if ( msgctx->context_pool != NULL )
		pool_unref(&(msgctx->context_pool));

	msgctx->context_pool =
		pool_alloconly_create("sieve_message_context_data", 1024);

	p_array_init(&msgctx->ext_contexts, msgctx->context_pool,
		sieve_extensions_get_count(msgctx->svinst));
}

void sieve_message_context_reset(struct sieve_message_context *msgctx)
{
	sieve_message_context_clear(msgctx);

	msgctx->pool = pool_alloconly_create("sieve_message_context", 1024);

	p_array_init(&msgctx->versions, msgctx->pool, 4);

	sieve_message_context_flush(msgctx);
}

pool_t sieve_message_context_pool(struct sieve_message_context *msgctx)
{
	return msgctx->context_pool;
}

/* Extension support */

void sieve_message_context_extension_set
(struct sieve_message_context *msgctx, const struct sieve_extension *ext,
	void *context)
{
	if ( ext->id < 0 ) return;

	array_idx_set(&msgctx->ext_contexts, (unsigned int) ext->id, &context);
}

const void *sieve_message_context_extension_get
(struct sieve_message_context *msgctx, const struct sieve_extension *ext)
{
	void * const *ctx;

	if  ( ext->id < 0 || ext->id >= (int) array_count(&msgctx->ext_contexts) )
		return NULL;

	ctx = array_idx(&msgctx->ext_contexts, (unsigned int) ext->id);

	return *ctx;
}

/* Envelope */

static void sieve_message_envelope_parse(struct sieve_message_context *msgctx)
{
	const struct sieve_message_data *msgdata = msgctx->msgdata;
	struct sieve_instance *svinst = msgctx->svinst;

	/* FIXME: log parse problems properly; logs only 'failure' now */

	msgctx->envelope_orig_recipient = sieve_address_parse_envelope_path
		(msgctx->pool, msgdata->orig_envelope_to);

	if ( msgctx->envelope_orig_recipient == NULL ) {
		sieve_sys_warning(svinst,
			"original envelope recipient address '%s' is unparsable",
			msgdata->orig_envelope_to);
	} else if ( msgctx->envelope_orig_recipient->local_part == NULL ) {
		sieve_sys_warning(svinst,
			"original envelope recipient address '%s' is a null path",
			msgdata->orig_envelope_to);
	}

	msgctx->envelope_final_recipient = sieve_address_parse_envelope_path
		(msgctx->pool, msgdata->final_envelope_to);

	if ( msgctx->envelope_final_recipient == NULL ) {
		if ( msgctx->envelope_orig_recipient != NULL ) {
			sieve_sys_warning(svinst,
				"final envelope recipient address '%s' is unparsable",
				msgdata->final_envelope_to);
		}
	} else if ( msgctx->envelope_final_recipient->local_part == NULL ) {
		if ( strcmp(msgdata->orig_envelope_to, msgdata->final_envelope_to) != 0 ) {
			sieve_sys_warning(svinst,
				"final envelope recipient address '%s' is a null path",
				msgdata->final_envelope_to);
		}
	}

	msgctx->envelope_sender = sieve_address_parse_envelope_path
		(msgctx->pool, msgdata->return_path);

	if ( msgctx->envelope_sender == NULL ) {
		sieve_sys_warning(svinst,
			"envelope sender address '%s' is unparsable",
			msgdata->return_path);
	}

	msgctx->envelope_parsed = TRUE;
}

const struct sieve_address *sieve_message_get_orig_recipient_address
(struct sieve_message_context *msgctx)
{
	if ( !msgctx->envelope_parsed )
		sieve_message_envelope_parse(msgctx);

	return msgctx->envelope_orig_recipient;
}

const struct sieve_address *sieve_message_get_final_recipient_address
(struct sieve_message_context *msgctx)
{
	if ( !msgctx->envelope_parsed )
		sieve_message_envelope_parse(msgctx);

	return msgctx->envelope_final_recipient;
}

const struct sieve_address *sieve_message_get_sender_address
(struct sieve_message_context *msgctx)
{
	if ( !msgctx->envelope_parsed )
		sieve_message_envelope_parse(msgctx);

	return msgctx->envelope_sender;
}

const char *sieve_message_get_orig_recipient
(struct sieve_message_context *msgctx)
{
	if ( !msgctx->envelope_parsed )
		sieve_message_envelope_parse(msgctx);

	return sieve_address_to_string(msgctx->envelope_orig_recipient);
}

const char *sieve_message_get_final_recipient
(struct sieve_message_context *msgctx)
{
	if ( !msgctx->envelope_parsed )
		sieve_message_envelope_parse(msgctx);

	return sieve_address_to_string(msgctx->envelope_final_recipient);
}

const char *sieve_message_get_sender
(struct sieve_message_context *msgctx)
{
	if ( !msgctx->envelope_parsed )
		sieve_message_envelope_parse(msgctx);

	return sieve_address_to_string(msgctx->envelope_sender);
}

/*
 * Mail
 */

int sieve_message_substitute
(struct sieve_message_context *msgctx, struct istream *input)
{
	static const char *wanted_headers[] = {
		"From", "Message-ID", "Subject", "Return-Path", NULL
	};
	struct mail_user *mail_user = msgctx->mail_user;
	struct sieve_message_version *version;
	struct mailbox_header_lookup_ctx *headers_ctx;
	struct mailbox *box = NULL;
	const char *sender;
	int ret;

	if ( msgctx->raw_mail_user == NULL ) {
		void **sets = master_service_settings_get_others(master_service);

		msgctx->raw_mail_user =
			raw_storage_create_from_set(mail_user->set_info, sets[0]);
	}

	i_stream_seek(input, 0);
	sender = sieve_message_get_sender(msgctx);
	sender = (sender == NULL ? DEFAULT_ENVELOPE_SENDER : sender );
	ret = raw_mailbox_alloc_stream(msgctx->raw_mail_user, input, (time_t)-1,
		sender, &box);

	if ( ret < 0 ) {
		sieve_sys_error(msgctx->svinst, "can't open substituted mail as raw: %s",
			mailbox_get_last_error(box, NULL));
		return -1;
	}

	if ( msgctx->substitute_snapshot ) {
		version = sieve_message_version_new(msgctx);
	} else {
		version = sieve_message_version_get(msgctx);
		sieve_message_version_free(version);
	}

	version->box = box;
	version->trans = mailbox_transaction_begin(box, 0);
	headers_ctx = mailbox_header_lookup_init(box, wanted_headers);
	version->mail = mail_alloc(version->trans, 0, headers_ctx);
	mailbox_header_lookup_unref(&headers_ctx);
	mail_set_seq(version->mail, 1);

	sieve_message_context_flush(msgctx);

	msgctx->substitute_snapshot = FALSE;
	msgctx->edit_snapshot = FALSE;

	return 1;
}

struct mail *sieve_message_get_mail
(struct sieve_message_context *msgctx)
{
	const struct sieve_message_version *versions;
	unsigned int count;

	versions = array_get(&msgctx->versions, &count);
	if ( count == 0 )
		return msgctx->msgdata->mail;

	if ( versions[count-1].edit_mail != NULL )
		return edit_mail_get_mail(versions[count-1].edit_mail);

	return versions[count-1].mail;
}

struct edit_mail *sieve_message_edit
(struct sieve_message_context *msgctx)
{
	struct sieve_message_version *version;

	version = sieve_message_version_get(msgctx);

	if ( version->edit_mail == NULL ) {
		version->edit_mail = edit_mail_wrap
			(( version->mail == NULL ? msgctx->msgdata->mail : version->mail ));
	} else if ( msgctx->edit_snapshot ) {
		version->edit_mail = edit_mail_snapshot(version->edit_mail);
	}

	msgctx->edit_snapshot = FALSE;

	return version->edit_mail;
}

void sieve_message_snapshot
(struct sieve_message_context *msgctx)
{
	msgctx->edit_snapshot = TRUE;
	msgctx->substitute_snapshot = TRUE;
}

/*
 * Header stringlist
 */

/* Forward declarations */

static int sieve_message_header_stringlist_next_item
	(struct sieve_stringlist *_strlist, string_t **str_r);
static void sieve_message_header_stringlist_reset
	(struct sieve_stringlist *_strlist);

/* String list object */

struct sieve_message_header_stringlist {
	struct sieve_stringlist strlist;

	struct sieve_stringlist *field_names;

	const char *const *headers;
	int headers_index;

	unsigned int mime_decode:1;
};

struct sieve_stringlist *sieve_message_header_stringlist_create
(const struct sieve_runtime_env *renv, struct sieve_stringlist *field_names,
	bool mime_decode)
{
	struct sieve_message_header_stringlist *strlist;

	strlist = t_new(struct sieve_message_header_stringlist, 1);
	strlist->strlist.runenv = renv;
	strlist->strlist.exec_status = SIEVE_EXEC_OK;
	strlist->strlist.next_item = sieve_message_header_stringlist_next_item;
	strlist->strlist.reset = sieve_message_header_stringlist_reset;
	strlist->field_names = field_names;
	strlist->mime_decode = mime_decode;

	return &strlist->strlist;
}

static inline string_t *_header_right_trim(const char *raw)
{
	string_t *result;
	int i;

	for ( i = strlen(raw)-1; i >= 0; i-- ) {
		if ( raw[i] != ' ' && raw[i] != '\t' ) break;
	}

	result = t_str_new(i+1);
	str_append_n(result, raw, i + 1);
	return result;
}

/* String list implementation */

static int sieve_message_header_stringlist_next_item
(struct sieve_stringlist *_strlist, string_t **str_r)
{
	struct sieve_message_header_stringlist *strlist =
		(struct sieve_message_header_stringlist *) _strlist;
	const struct sieve_runtime_env *renv = _strlist->runenv;
	struct mail *mail = sieve_message_get_mail(renv->msgctx);

	*str_r = NULL;

	/* Check for end of current header list */
	if ( strlist->headers == NULL ) {
		strlist->headers_index = 0;
 	} else if ( strlist->headers[strlist->headers_index] == NULL ) {
		strlist->headers = NULL;
		strlist->headers_index = 0;
	}

	/* Fetch next header */
	while ( strlist->headers == NULL ) {
		string_t *hdr_item = NULL;
		int ret;

		/* Read next header name from source list */
		if ( (ret=sieve_stringlist_next_item(strlist->field_names, &hdr_item))
			<= 0 )
			return ret;

		if ( _strlist->trace ) {
			sieve_runtime_trace(renv, 0,
				"extracting `%s' headers from message",
				str_sanitize(str_c(hdr_item), 80));
		}

		/* Fetch all matching headers from the e-mail */
		if ( strlist->mime_decode ) {
			ret = mail_get_headers_utf8(mail, str_c(hdr_item), &strlist->headers);			
		} else {
			ret = mail_get_headers(mail, str_c(hdr_item), &strlist->headers);
		}

		if (ret < 0) {
			_strlist->exec_status = sieve_runtime_mail_error
				(renv, mail, "failed to read header field `%s'", str_c(hdr_item));
			return -1;
		}

		if ( strlist->headers == NULL || strlist->headers[0] == NULL ) {
			/* Try next item when no headers found */
			strlist->headers = NULL;
		}
	}

	/* Return next item */
	*str_r = _header_right_trim(strlist->headers[strlist->headers_index++]);
	return 1;
}

static void sieve_message_header_stringlist_reset
(struct sieve_stringlist *_strlist)
{
	struct sieve_message_header_stringlist *strlist =
		(struct sieve_message_header_stringlist *) _strlist;

	strlist->headers = NULL;
	strlist->headers_index = 0;
	sieve_stringlist_reset(strlist->field_names);
}

/*
 * Header override operand
 */

const struct sieve_operand_class sieve_message_override_operand_class =
	{ "header-override" };

bool sieve_opr_message_override_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	struct sieve_message_override svmo;
	const struct sieve_message_override_def *hodef;

	if ( !sieve_opr_object_dump
		(denv, &sieve_message_override_operand_class, address, &svmo.object) )
		return FALSE;

	hodef = svmo.def =
		(const struct sieve_message_override_def *) svmo.object.def;

	if ( hodef->dump_context != NULL ) {
		sieve_code_descend(denv);
		if ( !hodef->dump_context(&svmo, denv, address) ) {
			return FALSE;
		}
		sieve_code_ascend(denv);
	}

	return TRUE;
}

int sieve_opr_message_override_read
(const struct sieve_runtime_env *renv, sieve_size_t *address,
	struct sieve_message_override *svmo)
{
	const struct sieve_message_override_def *hodef;
	int ret;

	svmo->context = NULL;

	if ( !sieve_opr_object_read
		(renv, &sieve_message_override_operand_class, address, &svmo->object) )
		return SIEVE_EXEC_BIN_CORRUPT;

	hodef = svmo->def =
		(const struct sieve_message_override_def *) svmo->object.def;

	if ( hodef->read_context != NULL &&
		(ret=hodef->read_context(svmo, renv, address, &svmo->context)) <= 0 )
		return ret;

	return SIEVE_EXEC_OK;
}

/*
 * Optional operands
 */

int sieve_message_opr_optional_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	signed int *opt_code)
{
	signed int _opt_code = 0;
	bool final = FALSE, opok = TRUE;

	if ( opt_code == NULL ) {
		opt_code = &_opt_code;
		final = TRUE;
	}

	while ( opok ) {
		int opt;

		if ( (opt=sieve_addrmatch_opr_optional_dump
			(denv, address, opt_code)) <= 0 )
			return opt;

		if ( *opt_code == SIEVE_OPT_MESSAGE_OVERRIDE ) {
			opok = sieve_opr_message_override_dump(denv, address);
		} else {
			return ( final ? -1 : 1 );
		}
	}

	return -1;
}

int sieve_message_opr_optional_read
(const struct sieve_runtime_env *renv, sieve_size_t *address,
	signed int *opt_code, int *exec_status,
	struct sieve_address_part *addrp, struct sieve_match_type *mcht,
	struct sieve_comparator *cmp, 
	ARRAY_TYPE(sieve_message_override) *svmos)
{
	signed int _opt_code = 0;
	bool final = FALSE;
	int ret;

	if ( opt_code == NULL ) {
		opt_code = &_opt_code;
		final = TRUE;
	}

	if ( exec_status != NULL )
		*exec_status = SIEVE_EXEC_OK;

	for ( ;; ) {
		int opt;

		if ( (opt=sieve_addrmatch_opr_optional_read
			(renv, address, opt_code, exec_status, addrp, mcht, cmp)) <= 0 )
			return opt;

		if ( *opt_code == SIEVE_OPT_MESSAGE_OVERRIDE ) {
			struct sieve_message_override svmo;
			const struct sieve_message_override *svmo_idx;
			unsigned int count, i;

			if ( (ret=sieve_opr_message_override_read
				(renv, address, &svmo)) <= 0 ) {
				if ( exec_status != NULL )
					*exec_status = ret;
				return -1;
			}

			if ( !array_is_created(svmos) )
				t_array_init(svmos, 8);
			/* insert in sorted sequence */
			svmo_idx = array_get(svmos, &count);
			for (i = 0; i < count; i++) {
				if (svmo.def->sequence < svmo_idx[i].def->sequence) {
					array_insert(svmos, i, &svmo, 1);
					break;
				}
			}
			if (count == i)
				array_append(svmos, &svmo, 1);
		} else {
			if ( final ) {
				sieve_runtime_trace_error(renv, "invalid optional operand");
				if ( exec_status != NULL )
					*exec_status = SIEVE_EXEC_BIN_CORRUPT;
				return -1;
			}
			return 1;
		}
	}

	i_unreached();
	return -1;
}

/*
 * Message header
 */

int sieve_message_get_header_fields
(const struct sieve_runtime_env *renv,
	struct sieve_stringlist *field_names,
	ARRAY_TYPE(sieve_message_override) *svmos,
	bool mime_decode, struct sieve_stringlist **fields_r)
{
	const struct sieve_message_override *svmo;
	unsigned int count, i;
	int ret;

	if ( svmos == NULL || !array_is_created(svmos) ||
		array_count(svmos) == 0 ) {
		*fields_r = sieve_message_header_stringlist_create
			(renv, field_names, mime_decode);
		return SIEVE_EXEC_OK;
	}

	svmo = array_get(svmos, &count);
	if ( svmo[0].def->sequence == 0 &&
		svmo[0].def->header_override != NULL ) {
		*fields_r = field_names;
	} else {
		*fields_r = sieve_message_header_stringlist_create
			(renv, field_names, mime_decode);
	}

	for ( i = 0; i < count; i++ ) {
		if ( svmo[i].def->header_override != NULL &&
			(ret=svmo[i].def->header_override
				(&svmo[i], renv, mime_decode, fields_r)) <= 0 )
			return ret;
	}
	return SIEVE_EXEC_OK;
}


