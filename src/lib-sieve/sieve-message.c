/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "ioloop.h"
#include "mempool.h"
#include "array.h"
#include "str.h"
#include "str-sanitize.h"
#include "istream.h"
#include "time-util.h"
#include "rfc822-parser.h"
#include "message-date.h"
#include "message-parser.h"
#include "message-decoder.h"
#include "message-header-decode.h"
#include "mail-html2text.h"
#include "mail-storage.h"
#include "mail-storage-service.h"
#include "mail-user.h"
#include "smtp-params.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "raw-storage.h"

#include "edit-mail.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-address.h"
#include "sieve-address-parts.h"
#include "sieve-runtime.h"
#include "sieve-runtime-trace.h"
#include "sieve-match.h"
#include "sieve-interpreter.h"

#include "sieve-message.h"

/*
 * Message transmission
 */

const char *sieve_message_get_new_id(const struct sieve_instance *svinst)
{
	static int count = 0;

	return t_strdup_printf("<dovecot-sieve-%s-%s-%d@%s>",
			       dec2str(ioloop_timeval.tv_sec),
			       dec2str(ioloop_timeval.tv_usec),
			       count++, svinst->hostname);
}

/*
 * Message context
 */

struct sieve_message_header {
	const char *name;

	const unsigned char *value, *utf8_value;
	size_t value_len, utf8_value_len;
};

struct sieve_message_part {
	struct sieve_message_part *parent, *next, *children;

	ARRAY(struct sieve_message_header) headers;

	const char *content_type;
	const char *content_disposition;

	const char *decoded_body;
	const char *text_body;
	size_t decoded_body_size;
	size_t text_body_size;

	bool have_body:1; /* there's the empty end-of-headers line */
	bool epilogue:1;  /* this is a multipart epilogue */
};

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
	struct timeval time;

	struct mail_user *mail_user;
	const struct sieve_message_data *msgdata;

	/* Message versioning */

	struct mail_user *raw_mail_user;
	ARRAY(struct sieve_message_version) versions;

	/* Context data for extensions */

	ARRAY(void *) ext_contexts;

	/* Body */

	ARRAY(struct sieve_message_part *) cached_body_parts;
	ARRAY(struct sieve_message_part_data) return_body_parts;
	buffer_t *raw_body;

	bool edit_snapshot:1;
	bool substitute_snapshot:1;
};

/*
 * Message versions
 */

static inline struct sieve_message_version *
sieve_message_version_new(struct sieve_message_context *msgctx)
{
	return array_append_space(&msgctx->versions);
}

static inline struct sieve_message_version *
sieve_message_version_get(struct sieve_message_context *msgctx)
{
	struct sieve_message_version *versions;
	unsigned int count;

	versions = array_get_modifiable(&msgctx->versions, &count);
	if (count == 0)
		return array_append_space(&msgctx->versions);

	return &versions[count-1];
}

static inline void
sieve_message_version_free(struct sieve_message_version *version)
{
	if (version->edit_mail != NULL) {
		edit_mail_unwrap(&version->edit_mail);
		version->edit_mail = NULL;
	}

	if (version->mail != NULL) {
		mail_free(&version->mail);
		mailbox_transaction_rollback(&version->trans);
		mailbox_free(&version->box);
		version->mail = NULL;
	}
}

/*
 * Message context object
 */

struct sieve_message_context *
sieve_message_context_create(struct sieve_instance *svinst,
			     struct mail_user *mail_user,
			     const struct sieve_message_data *msgdata)
{
	struct sieve_message_context *msgctx;

	msgctx = i_new(struct sieve_message_context, 1);
	msgctx->refcount = 1;
	msgctx->svinst = svinst;

	msgctx->mail_user = mail_user;
	msgctx->msgdata = msgdata;

	i_gettimeofday(&msgctx->time);

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

	if (msgctx->pool != NULL) {
		versions = array_get_modifiable(&msgctx->versions, &count);

		for (i = 0; i < count; i++)
			sieve_message_version_free(&versions[i]);

		pool_unref(&(msgctx->pool));
	}
}

void sieve_message_context_unref(struct sieve_message_context **msgctx)
{
	i_assert((*msgctx)->refcount > 0);

	if (--(*msgctx)->refcount != 0)
		return;

	if ((*msgctx)->raw_mail_user != NULL)
		mail_user_unref(&(*msgctx)->raw_mail_user);

	sieve_message_context_clear(*msgctx);

	if ((*msgctx)->context_pool != NULL)
		pool_unref(&((*msgctx)->context_pool));

	i_free(*msgctx);
	*msgctx = NULL;
}

static void sieve_message_context_flush(struct sieve_message_context *msgctx)
{
	pool_t pool;

	if (msgctx->context_pool != NULL)
		pool_unref(&(msgctx->context_pool));

	msgctx->context_pool = pool =
		pool_alloconly_create("sieve_message_context_data", 2048);

	p_array_init(&msgctx->ext_contexts, pool,
		sieve_extensions_get_count(msgctx->svinst));

	p_array_init(&msgctx->cached_body_parts, pool, 8);
	p_array_init(&msgctx->return_body_parts, pool, 8);
	msgctx->raw_body = NULL;
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

void sieve_message_context_time(struct sieve_message_context *msgctx,
				struct timeval *time)
{
	*time = msgctx->time;
}

/* Extension support */

void sieve_message_context_extension_set(struct sieve_message_context *msgctx,
					 const struct sieve_extension *ext,
					 void *context)
{
	if (ext->id < 0)
		return;

	array_idx_set(&msgctx->ext_contexts, (unsigned int)ext->id, &context);
}

void *sieve_message_context_extension_get(struct sieve_message_context *msgctx,
					  const struct sieve_extension *ext)
{
	void *const *ctx;

	if  (ext->id < 0 || ext->id >= (int)array_count(&msgctx->ext_contexts))
		return NULL;

	ctx = array_idx(&msgctx->ext_contexts, (unsigned int)ext->id);
	return *ctx;
}

/* Envelope */

const struct smtp_address *
sieve_message_get_orig_recipient(struct sieve_message_context *msgctx)
{
	const struct sieve_message_data *msgdata = msgctx->msgdata;
	const struct smtp_address *orcpt_to = NULL;

	if (msgdata->envelope.rcpt_params != NULL) {
		orcpt_to = msgdata->envelope.rcpt_params->orcpt.addr;
		if (!smtp_address_isnull(orcpt_to))
			return orcpt_to;
	}

	orcpt_to = msgdata->envelope.rcpt_to;
	return (!smtp_address_isnull(orcpt_to) ? orcpt_to : NULL);
}

const struct smtp_address *
sieve_message_get_final_recipient(struct sieve_message_context *msgctx)
{
	const struct sieve_message_data *msgdata = msgctx->msgdata;
	const struct smtp_address *rcpt_to = msgdata->envelope.rcpt_to;

	return (!smtp_address_isnull(rcpt_to) ? rcpt_to : NULL);
}

const struct smtp_address *
sieve_message_get_sender(struct sieve_message_context *msgctx)
{
	const struct sieve_message_data *msgdata = msgctx->msgdata;
	const struct smtp_address *mail_from = msgdata->envelope.mail_from;

	return (!smtp_address_isnull(mail_from) ? mail_from : NULL);
}

/*
 * Mail
 */

int sieve_message_substitute(struct sieve_message_context *msgctx,
			     struct istream *input)
{
	static const char *wanted_headers[] = {
		"From", "Message-ID", "Subject", "Return-Path", NULL
	};
	static const struct smtp_address default_sender = {
		.localpart = DEFAULT_ENVELOPE_SENDER,
		.domain = NULL,
	};
	struct mail_user *mail_user = msgctx->mail_user;
	struct sieve_message_version *version;
	struct mailbox_header_lookup_ctx *headers_ctx;
	struct mailbox *box = NULL;
	const struct smtp_address *sender;
	int ret;

	i_assert(input->blocking);

	if (msgctx->raw_mail_user == NULL) {
		struct mail_storage_service_ctx *storage_service =
			mail_storage_service_user_get_service_ctx(
				mail_user->service_user);
		struct settings_instance *set_instance =
			mail_storage_service_user_get_settings_instance(mail_user->service_user);
		msgctx->raw_mail_user =
			raw_storage_create_from_set(storage_service, set_instance);
	}

	i_stream_seek(input, 0);
	sender = sieve_message_get_sender(msgctx);
	sender = (sender == NULL ? &default_sender : sender);
	ret = raw_mailbox_alloc_stream(msgctx->raw_mail_user, input, (time_t)-1,
				       smtp_address_encode(sender), &box);

	if (ret < 0) {
		e_error(msgctx->svinst->event,
			"can't open substituted mail as raw: %s",
			mailbox_get_last_internal_error(box, NULL));
		return -1;
	}

	if (msgctx->substitute_snapshot) {
		version = sieve_message_version_new(msgctx);
	} else {
		version = sieve_message_version_get(msgctx);
		sieve_message_version_free(version);
	}

	version->box = box;
	version->trans = mailbox_transaction_begin(box, 0, __func__);
	headers_ctx = mailbox_header_lookup_init(box, wanted_headers);
	version->mail = mail_alloc(version->trans, 0, headers_ctx);
	mailbox_header_lookup_unref(&headers_ctx);
	mail_set_seq(version->mail, 1);

	sieve_message_context_flush(msgctx);

	msgctx->substitute_snapshot = FALSE;
	msgctx->edit_snapshot = FALSE;
	return 1;
}

struct mail *sieve_message_get_mail(struct sieve_message_context *msgctx)
{
	const struct sieve_message_version *versions;
	unsigned int count;

	versions = array_get(&msgctx->versions, &count);
	if (count == 0)
		return msgctx->msgdata->mail;

	if (versions[count-1].edit_mail != NULL)
		return edit_mail_get_mail(versions[count-1].edit_mail);
	return versions[count-1].mail;
}

struct edit_mail *sieve_message_edit(struct sieve_message_context *msgctx)
{
	struct sieve_message_version *version;

	version = sieve_message_version_get(msgctx);

	if (version->edit_mail == NULL) {
		version->edit_mail = edit_mail_wrap(
			(version->mail == NULL ?
			 msgctx->msgdata->mail : version->mail));
	} else if (msgctx->edit_snapshot) {
		version->edit_mail = edit_mail_snapshot(version->edit_mail);
	}

	msgctx->edit_snapshot = FALSE;
	return version->edit_mail;
}

void sieve_message_snapshot(struct sieve_message_context *msgctx)
{
	msgctx->edit_snapshot = TRUE;
	msgctx->substitute_snapshot = TRUE;
}

/*
 * Message header list
 */

/* Forward declarations */

static int
sieve_message_header_list_next_item(struct sieve_header_list *_hdrlist,
				    const char **name_r, string_t **value_r);
static int
sieve_message_header_list_next_value(struct sieve_stringlist *_strlist,
				     string_t **value_r);
static void
sieve_message_header_list_reset(struct sieve_stringlist *_strlist);

/* String list object */

struct sieve_message_header_list {
	struct sieve_header_list hdrlist;

	struct sieve_stringlist *field_names;

	const char *header_name;
	const char *const *headers;
	int headers_index;

	bool mime_decode:1;
};

struct sieve_header_list *
sieve_message_header_list_create(const struct sieve_runtime_env *renv,
				 struct sieve_stringlist *field_names,
				 bool mime_decode)
{
	struct sieve_message_header_list *hdrlist;

	hdrlist = t_new(struct sieve_message_header_list, 1);
	hdrlist->hdrlist.strlist.runenv = renv;
	hdrlist->hdrlist.strlist.exec_status = SIEVE_EXEC_OK;
	hdrlist->hdrlist.strlist.next_item =
		sieve_message_header_list_next_value;
	hdrlist->hdrlist.strlist.reset = sieve_message_header_list_reset;
	hdrlist->hdrlist.next_item = sieve_message_header_list_next_item;
	hdrlist->field_names = field_names;
	hdrlist->mime_decode = mime_decode;

	return &hdrlist->hdrlist;
}

// NOTE: get rid of this once we have a proper Sieve string type
static inline string_t *_header_right_trim(const char *raw)
{
	string_t *result;
	const char *p, *pend;

	pend = raw + strlen(raw);
	if (raw == pend) {
		result = t_str_new(1);
	} else {
		for (p = pend-1; p >= raw; p--) {
			if (*p != ' ' && *p != '\t')
				break;
		}
		result = t_str_new(p - raw + 1);
		str_append_data(result, raw, p - raw + 1);
	}
	return result;
}

/* String list implementation */

static int
sieve_message_header_list_next_item(struct sieve_header_list *_hdrlist,
				    const char **name_r, string_t **value_r)
{
	struct sieve_message_header_list *hdrlist =
		(struct sieve_message_header_list *)_hdrlist;
	const struct sieve_runtime_env *renv = _hdrlist->strlist.runenv;
	struct mail *mail = sieve_message_get_mail(renv->msgctx);

	if (name_r != NULL)
		*name_r = NULL;
	*value_r = NULL;

	/* Check for end of current header list */
	if (hdrlist->headers == NULL) {
		hdrlist->headers_index = 0;
 	} else if (hdrlist->headers[hdrlist->headers_index] == NULL) {
		hdrlist->headers = NULL;
		hdrlist->headers_index = 0;
	}

	/* Fetch next header */
	while (hdrlist->headers == NULL) {
		string_t *hdr_item = NULL;
		int ret;

		/* Read next header name from source list */
		if ((ret = sieve_stringlist_next_item(hdrlist->field_names,
						      &hdr_item)) <= 0)
			return ret;

		hdrlist->header_name = str_c(hdr_item);

		if (_hdrlist->strlist.trace) {
			sieve_runtime_trace
				(renv, 0,
				"extracting '%s' headers from message",
				str_sanitize(str_c(hdr_item), 80));
		}

		/* Fetch all matching headers from the e-mail */
		if (hdrlist->mime_decode) {
			ret = mail_get_headers_utf8(mail, str_c(hdr_item),
						    &hdrlist->headers);
		} else {
			ret = mail_get_headers(mail, str_c(hdr_item),
					       &hdrlist->headers);
		}

		if (ret < 0) {
			_hdrlist->strlist.exec_status =
				sieve_runtime_mail_error(
					renv, mail,
					"failed to read header field '%s'",
					str_c(hdr_item));
			return -1;
		}

		if (ret == 0 || hdrlist->headers[0] == NULL) {
			/* Try next item when no headers found */
			hdrlist->headers = NULL;
		}
	}

	/* Return next item */
	if (name_r != NULL)
		*name_r = hdrlist->header_name;
	*value_r = _header_right_trim(
		hdrlist->headers[hdrlist->headers_index++]);
	return 1;
}

static int
sieve_message_header_list_next_value(struct sieve_stringlist *_strlist,
				     string_t **value_r)
{
	struct sieve_header_list *hdrlist =
		(struct sieve_header_list *)_strlist;

	return sieve_message_header_list_next_item(hdrlist, NULL, value_r);
}

static void
sieve_message_header_list_reset(struct sieve_stringlist *strlist)
{
	struct sieve_message_header_list *hdrlist =
		(struct sieve_message_header_list *)strlist;

	hdrlist->headers = NULL;
	hdrlist->headers_index = 0;
	sieve_stringlist_reset(hdrlist->field_names);
}

/*
 * Header override operand
 */

const struct sieve_operand_class sieve_message_override_operand_class =
	{ "header-override" };

bool sieve_opr_message_override_dump(const struct sieve_dumptime_env *denv,
				     sieve_size_t *address)
{
	struct sieve_message_override svmo;
	const struct sieve_message_override_def *hodef;

	if (!sieve_opr_object_dump(denv, &sieve_message_override_operand_class,
				   address, &svmo.object))
		return FALSE;

	hodef = svmo.def =
		(const struct sieve_message_override_def *)svmo.object.def;

	if (hodef->dump_context != NULL) {
		sieve_code_descend(denv);
		if (!hodef->dump_context(&svmo, denv, address))
			return FALSE;
		sieve_code_ascend(denv);
	}
	return TRUE;
}

int sieve_opr_message_override_read(const struct sieve_runtime_env *renv,
				    sieve_size_t *address,
				    struct sieve_message_override *svmo)
{
	const struct sieve_message_override_def *hodef;
	int ret;

	svmo->context = NULL;

	if (!sieve_opr_object_read(renv, &sieve_message_override_operand_class,
				   address, &svmo->object))
		return SIEVE_EXEC_BIN_CORRUPT;

	hodef = svmo->def =
		(const struct sieve_message_override_def *)svmo->object.def;

	if (hodef->read_context != NULL &&
	    (ret = hodef->read_context(svmo, renv, address,
				       &svmo->context)) <= 0)
		return ret;
	return SIEVE_EXEC_OK;
}

/*
 * Optional operands
 */

int sieve_message_opr_optional_dump(const struct sieve_dumptime_env *denv,
				    sieve_size_t *address,
				    signed int *opt_code)
{
	signed int _opt_code = 0;
	bool final = FALSE, opok = TRUE;

	if (opt_code == NULL) {
		opt_code = &_opt_code;
		final = TRUE;
	}

	while (opok) {
		int opt;

		if ((opt = sieve_addrmatch_opr_optional_dump(denv, address,
							     opt_code)) <= 0)
			return opt;

		if (*opt_code == SIEVE_OPT_MESSAGE_OVERRIDE) {
			opok = sieve_opr_message_override_dump(denv, address);
		} else {
			return (final ? -1 : 1);
		}
	}
	return -1;
}

int sieve_message_opr_optional_read(const struct sieve_runtime_env *renv,
				    sieve_size_t *address,
				    signed int *opt_code, int *exec_status,
				    struct sieve_address_part *addrp,
				    struct sieve_match_type *mcht,
				    struct sieve_comparator *cmp,
				    ARRAY_TYPE(sieve_message_override) *svmos)
{
	signed int _opt_code = 0;
	bool final = FALSE;
	int ret;

	if (opt_code == NULL) {
		opt_code = &_opt_code;
		final = TRUE;
	}

	if (exec_status != NULL)
		*exec_status = SIEVE_EXEC_OK;

	for (;;) {
		int opt;

		if ((opt = sieve_addrmatch_opr_optional_read(
			renv, address, opt_code, exec_status,
			addrp, mcht, cmp)) <= 0)
			return opt;

		if (*opt_code == SIEVE_OPT_MESSAGE_OVERRIDE) {
			struct sieve_message_override svmo;
			const struct sieve_message_override *svmo_idx;
			unsigned int count, i;

			if ((ret = sieve_opr_message_override_read(
				renv, address, &svmo)) <= 0) {
				if (exec_status != NULL)
					*exec_status = ret;
				return -1;
			}

			if (!array_is_created(svmos))
				t_array_init(svmos, 8);
			/* insert in sorted sequence */
			svmo_idx = array_get(svmos, &count);
			for (i = 0; i < count; i++) {
				if (svmo.def->sequence <
				    svmo_idx[i].def->sequence) {
					array_insert(svmos, i, &svmo, 1);
					break;
				}
			}
			if (count == i)
				array_append(svmos, &svmo, 1);
		} else {
			if (final) {
				sieve_runtime_trace_error(
					renv, "invalid optional operand");
				if (exec_status != NULL)
					*exec_status = SIEVE_EXEC_BIN_CORRUPT;
				return -1;
			}
			return 1;
		}
	}
	i_unreached();
}

/*
 * Message header
 */

int sieve_message_get_header_fields(const struct sieve_runtime_env *renv,
				    struct sieve_stringlist *field_names,
				    ARRAY_TYPE(sieve_message_override) *svmos,
				    bool mime_decode,
				    struct sieve_stringlist **fields_r)
{
	const struct sieve_message_override *svmo;
	unsigned int count, i;
	int ret;

	if (svmos == NULL || !array_is_created(svmos) ||
	    array_count(svmos) == 0) {
		struct sieve_header_list *headers;

		headers = sieve_message_header_list_create(
			renv, field_names, mime_decode);
		*fields_r = &headers->strlist;
		return SIEVE_EXEC_OK;
	}

	svmo = array_get(svmos, &count);
	if (svmo[0].def->sequence == 0 &&
	    svmo[0].def->header_override != NULL) {
		*fields_r = field_names;
	} else {
		struct sieve_header_list *headers;

		headers = sieve_message_header_list_create(renv, field_names,
							   mime_decode);
		*fields_r = &headers->strlist;
	}

	for (i = 0; i < count; i++) {
		if (svmo[i].def->header_override != NULL &&
		    (ret = svmo[i].def->header_override(
			&svmo[i], renv, mime_decode, fields_r)) <= 0)
			return ret;
	}
	return SIEVE_EXEC_OK;
}

/*
 * Message part
 */

struct sieve_message_part *
sieve_message_part_parent(struct sieve_message_part *mpart)
{
	return mpart->parent;
}

struct sieve_message_part *
sieve_message_part_next(struct sieve_message_part *mpart)
{
	return mpart->next;
}

struct sieve_message_part *
sieve_message_part_children(struct sieve_message_part *mpart)
{
	return mpart->children;
}

const char *
sieve_message_part_content_type(struct sieve_message_part *mpart)
{
	return mpart->content_type;
}

const char *
sieve_message_part_content_disposition(struct sieve_message_part *mpart)
{
	return mpart->content_disposition;
}

int sieve_message_part_get_first_header(struct sieve_message_part *mpart,
					const char *field,
					const char **value_r)
{
	const struct sieve_message_header *headers;
	unsigned int i, count;

	headers = array_get(&mpart->headers, &count);
	for (i = 0; i < count; i++) {
		if (strcasecmp(headers[i].name, field) == 0) {
			i_assert(headers[i].value[headers[i].value_len] == '\0');
			*value_r = (const char *)headers[i].value;
			return 1;
		}
	}

	*value_r = NULL;
	return 0;
}

void sieve_message_part_get_data(struct sieve_message_part *mpart,
				 struct sieve_message_part_data *data,
				 bool text)
{
	i_zero(data);
	data->content_type = mpart->content_type;
	data->content_disposition = mpart->content_disposition;

	if (!text) {
		data->content = mpart->decoded_body;
		data->size = mpart->decoded_body_size;
	} else if (mpart->children != NULL) {
		data->content = "";
		data->size = 0;
	} else {
		data->content = mpart->text_body;
		data->size = mpart->text_body_size;
	}
}

/*
 * Message body
 */

static void str_replace_nuls(string_t *str)
{
	char *data = str_c_modifiable(str);
	unsigned int i, len = str_len(str);

	for (i = 0; i < len; i++) {
		if (data[i] == '\0')
			data[i] = ' ';
	}
}

static bool
_is_wanted_content_type(const char *const *wanted_types,
			const char *content_type) ATTR_NULL(1)
{
	const char *subtype;
	size_t type_len;

	if (wanted_types == NULL)
		return TRUE;

	subtype = strchr(content_type, '/');
	type_len = (subtype == NULL ?
		    strlen(content_type) : (size_t)(subtype - content_type));

	i_assert(wanted_types != NULL);

	for (; *wanted_types != NULL; wanted_types++) {
		const char *wanted_subtype;

		if (**wanted_types == '\0') {
			/* empty string matches everything */
			return TRUE;
		}

		wanted_subtype = strchr(*wanted_types, '/');
		if (wanted_subtype == NULL) {
			/* match only main type */
			if (strlen(*wanted_types) == type_len &&
			    strncasecmp(*wanted_types, content_type,
					type_len) == 0)
				return TRUE;
		} else {
			/* match whole type/subtype */
			if (strcasecmp(*wanted_types, content_type) == 0)
				return TRUE;
		}
	}
	return FALSE;
}

static bool
sieve_message_body_get_return_parts(const struct sieve_runtime_env *renv,
				    const char *const *wanted_types,
				    bool extract_text)
{
	struct sieve_message_context *msgctx = renv->msgctx;
	struct sieve_message_part *const *body_parts;
	unsigned int i, count;
	struct sieve_message_part_data *return_part;

	/* Check whether any body parts are cached already */
	body_parts = array_get(&msgctx->cached_body_parts, &count);
	if (count == 0)
		return FALSE;

	/* Clear result array */
	array_clear(&msgctx->return_body_parts);

	/* Fill result array with requested content_types */
	for (i = 0; i < count; i++) {
		if (!body_parts[i]->have_body) {
			/* Part has no body; according to RFC this MUST not
			   match to anything and therefore it is not included in
			   the result. */
			continue;
		}

		/* Skip content types that are not requested */
		if (!_is_wanted_content_type(wanted_types,
					     body_parts[i]->content_type))
			continue;

		/* Add new item to the result */
		return_part = array_append_space(&msgctx->return_body_parts);
		return_part->content_type = body_parts[i]->content_type;
		return_part->content_disposition =
			body_parts[i]->content_disposition;

		/* Depending on whether a decoded body part is requested, the
		   appropriate cache item is read. If it is missing, this
		   function fails and the cache needs to be completed by
		   sieve_message_parts_add_missing().
		 */
		if (extract_text) {
			if (body_parts[i]->text_body == NULL)
				return FALSE;
			return_part->content = body_parts[i]->text_body;
			return_part->size = body_parts[i]->text_body_size;
		} else {
			if (body_parts[i]->decoded_body == NULL)
				return FALSE;
			return_part->content = body_parts[i]->decoded_body;
			return_part->size = body_parts[i]->decoded_body_size;
		}
	}
	return TRUE;
}

static void
sieve_message_part_save(const struct sieve_runtime_env *renv, buffer_t *buf,
			struct sieve_message_part *body_part, bool extract_text)
{
	struct sieve_message_context *msgctx = renv->msgctx;
	pool_t pool = msgctx->context_pool;
	buffer_t *result_buf, *text_buf = NULL;
	char *part_data;
	size_t part_size;

	/* Extract text if requested */
	result_buf = buf;
	if (extract_text && body_part->children == NULL &&
	    !body_part->epilogue) {
		if (buf->used > 0 &&
		    mail_html2text_content_type_match(body_part->content_type)) {
			struct mail_html2text *html2text;

			text_buf = buffer_create_dynamic(default_pool, 4096);

			/* Remove HTML markup */
			html2text = mail_html2text_init(0);
			mail_html2text_more(html2text, buf->data, buf->used,
					    text_buf);
			mail_html2text_deinit(&html2text);

			result_buf = text_buf;
		}
	}

	/* Add terminating NUL to the body part buffer */
	buffer_append_c(result_buf, '\0');

	/* Make copy of the buffer */
	part_data = p_malloc(pool, result_buf->used);
	memcpy(part_data, result_buf->data, result_buf->used);
	part_size = result_buf->used - 1;

	/* Free text buffer if used */
	if (text_buf != NULL)
		buffer_free(&text_buf);

	/* Depending on whether the part is processed into text, store message
	   body in the appropriate cache location. */
	if (!extract_text) {
		body_part->decoded_body = part_data;
		body_part->decoded_body_size = part_size;
	} else {
		body_part->text_body = part_data;
		body_part->text_body_size = part_size;
	}

	/* Clear buffer */
	buffer_set_used_size(buf, 0);
}

static const char *_parse_content_type(const struct message_header_line *hdr)
{
	struct rfc822_parser_context parser;
	string_t *content_type;

	/* Initialize parsing */
	rfc822_parser_init(&parser, hdr->full_value, hdr->full_value_len, NULL);
	(void)rfc822_skip_lwsp(&parser);

	/* Parse content type */
	content_type = t_str_new(64);
	if (rfc822_parse_content_type(&parser, content_type) < 0)
		return "";

	/* Content-type value must end here, otherwise it is invalid after all
	 */
	(void)rfc822_skip_lwsp(&parser);
	if (parser.data != parser.end && *parser.data != ';')
		return "";

	/* Success */
	return str_c(content_type);
}

static const char *
_parse_content_disposition(const struct message_header_line *hdr)
{
	struct rfc822_parser_context parser;
	string_t *content_disp;

	/* Initialize parsing */
	rfc822_parser_init(&parser, hdr->full_value, hdr->full_value_len, NULL);
	(void)rfc822_skip_lwsp(&parser);

	/* Parse content type */
	content_disp = t_str_new(64);
	if (rfc822_parse_mime_token(&parser, content_disp) < 0)
		return "";

	/* Content-type value must end here, otherwise it is invalid after all */
	(void)rfc822_skip_lwsp(&parser);
	if (parser.data != parser.end && *parser.data != ';')
		return "";

	/* Success */
	return str_c(content_disp);
}

/* sieve_message_parts_add_missing():
 *   Add requested message body parts to the cache that are missing.
 */
static int
sieve_message_parts_add_missing(const struct sieve_runtime_env *renv,
				const char *const *content_types,
				bool extract_text, bool iter_all) ATTR_NULL(2)
{
	struct sieve_message_context *msgctx = renv->msgctx;
	pool_t pool = msgctx->context_pool;
	struct mail *mail = sieve_message_get_mail(renv->msgctx);
	struct message_parser_settings mparser_set = {
		.hdr_flags = MESSAGE_HEADER_PARSER_FLAG_SKIP_INITIAL_LWSP,
		.flags = MESSAGE_PARSER_FLAG_INCLUDE_MULTIPART_BLOCKS,
	};
	ARRAY(struct sieve_message_header) headers;
	struct sieve_message_part *body_part, *header_part, *last_part;
	struct message_parser_ctx *parser;
	struct message_decoder_context *decoder;
	struct message_block block, decoded;
	struct message_part *mparts, *prev_mpart = NULL;
	buffer_t *buf;
	struct istream *input;
	unsigned int idx = 0;
	bool save_body = FALSE, have_all;
	string_t *hdr_content = NULL;

	/* First check whether any are missing */
	if (!iter_all && sieve_message_body_get_return_parts(
		renv, content_types, extract_text)) {
		/* Cache hit; all are present */
		return SIEVE_EXEC_OK;
	}

	/* Get the message stream */
	if (mail_get_stream(mail, NULL, NULL, &input) < 0) {
		return sieve_runtime_mail_error(
			renv, mail, "failed to open input message");
	}
	if (mail_get_parts(mail, &mparts) < 0) {
		return sieve_runtime_mail_error(
			renv, mail, "failed to parse input message parts");
	}

	buf = buffer_create_dynamic(default_pool, 4096);
	body_part = header_part = last_part = NULL;

	if (iter_all) {
		t_array_init(&headers, 64);
		hdr_content = t_str_new(512);
		mparser_set.hdr_flags |= MESSAGE_HEADER_PARSER_FLAG_CLEAN_ONELINE;
	} else {
		i_zero(&headers);
	}

	/* Initialize body decoder */
	decoder = message_decoder_init(NULL, 0);

	// FIXME: currently not tested with edit-mail.
		//parser = message_parser_init_from_parts(parts, input,
		// hparser_flags, mparser_flags);
	parser = message_parser_init(pool_datastack_create(),
				     input, &mparser_set);
	while (message_parser_parse_next_block(parser, &block) > 0) {
		struct sieve_message_part **body_part_idx;
		struct message_header_line *hdr = block.hdr;
		struct sieve_message_header *header;
		unsigned char *data;

		if (block.part != prev_mpart) {
			bool message_rfc822 = FALSE;

			/* Save previous body part */
			if (body_part != NULL) {
				/* Treat message/rfc822 separately; headers
				   become content */
				if (block.part->parent == prev_mpart &&
				    strcmp(body_part->content_type,
					   "message/rfc822") == 0) {
					message_rfc822 = TRUE;
				} else if (save_body) {
					sieve_message_part_save(
						renv, buf, body_part,
						extract_text);
				}
				if (iter_all &&
				    !array_is_created(&body_part->headers) &&
				    array_count(&headers) > 0) {
					p_array_init(&body_part->headers, pool,
						     array_count(&headers));
					array_copy(&body_part->headers.arr, 0,
						   &headers.arr, 0,
						   array_count(&headers));
				}
			}

			/* Start processing next part */
			body_part_idx = array_idx_get_space(
				&msgctx->cached_body_parts, idx);
			if (*body_part_idx == NULL) {
				*body_part_idx = p_new(
					pool, struct sieve_message_part, 1);
			}
			body_part = *body_part_idx;
			body_part->content_type = "text/plain";
			if (iter_all)
				array_clear(&headers);

			/* Copy tree structure */
			if (block.part->context != NULL) {
				struct sieve_message_part *epipart =
					(struct sieve_message_part *)
						block.part->context;
				i_assert(epipart != NULL);

				/* multipart epilogue */
				body_part->content_type = epipart->content_type;
				body_part->have_body = TRUE;
				body_part->epilogue = TRUE;
				save_body = iter_all ||
					_is_wanted_content_type(content_types,
								body_part->content_type);

			} else {
				struct sieve_message_part *parent = NULL;

				if (block.part->parent != NULL) {
					body_part->parent = parent =
						(struct sieve_message_part *)
							block.part->parent->context;
				}

				/* new part */
				block.part->context = body_part;

				if (last_part != NULL) {
					i_assert(parent != NULL);
					if (last_part->parent == parent) {
						last_part->next = body_part;
					} else if (parent->children == NULL) {
						parent->children = body_part;
					} else {
						struct sieve_message_part *child = parent->children;
						while (child->next != NULL && child != body_part)
							child = child->next;
						if (child != body_part)
							child->next = body_part;
					}
				}
			}
			last_part = body_part;

			/* If this is message/rfc822 content, retain the
			   enveloping part for storing headers as content.
			 */
			if (message_rfc822) {
				i_assert(idx > 0);
				body_part_idx = array_idx_modifiable(
					&msgctx->cached_body_parts, idx-1);
				header_part = *body_part_idx;
			} else {
				header_part = NULL;
			}

			prev_mpart = block.part;
			idx++;
		}

		if (hdr != NULL || block.size == 0) {
			enum {
				_HDR_CONTENT_TYPE,
				_HDR_CONTENT_DISPOSITION,
				_HDR_OTHER
			} hdr_field;

			/* Reading headers */
			i_assert(body_part != NULL);

			/* Decode block */
			(void)message_decoder_decode_next_block(decoder, &block,
								&decoded);

			/* Check for end of headers */
			if (hdr == NULL) {
				/* Save headers for message/rfc822 part */
				if (header_part != NULL) {
					sieve_message_part_save(renv, buf,
								header_part,
								FALSE);
					header_part = NULL;
				}

				/* Save bodies only if we have a wanted content
				   type */
				save_body = iter_all ||
					_is_wanted_content_type(content_types,
								body_part->content_type);
				continue;
			}

			/* Encountered the empty line that indicates the end of
			   the headers and the start of the body
			 */
			if (hdr->eoh) {
				body_part->have_body = TRUE;
				continue;
			} else if (header_part != NULL) {
				/* Save message/rfc822 header as part content */
				if (hdr->continued) {
					buffer_append(buf, hdr->value,
						      hdr->value_len);
				} else {
					buffer_append(buf, hdr->name,
						      hdr->name_len);
					buffer_append(buf, hdr->middle,
						      hdr->middle_len);
					buffer_append(buf, hdr->value,
						      hdr->value_len);
				}
				if (!hdr->no_newline)
					buffer_append(buf, "\r\n", 2);
			}

			if (strcasecmp(hdr->name, "Content-Type") == 0)
				hdr_field = _HDR_CONTENT_TYPE;
			else if (strcasecmp(hdr->name,
					    "Content-Disposition") == 0)
				hdr_field = _HDR_CONTENT_DISPOSITION;
			else if (iter_all &&
				 !array_is_created(&body_part->headers))
				hdr_field = _HDR_OTHER;
			else {
				/* Not interested in this header */
				continue;
			}

			/* Header can have folding whitespace. Acquire the full
			   value before continuing
			 */
			if (hdr->continues) {
				hdr->use_full_value = TRUE;
				continue;
			}

			if (iter_all &&
			    !array_is_created(&body_part->headers)) {
				const unsigned char *value, *vp;
				size_t vlen;

				/* Add header */
				header = array_append_space(&headers);
				header->name = p_strdup(pool, hdr->name);

				/* Trim end of field value (not done by parser)
				 */
				value = hdr->full_value;
				vp = value + hdr->full_value_len;
				while (vp > value &&
				       (vp[-1] == '\t' || vp[-1] == ' '))
					vp--;
				vlen = (size_t)(vp - value);

				/* Decode MIME encoded-words. */
				str_truncate(hdr_content, 0);
				message_header_decode_utf8(value, vlen,
							   hdr_content, NULL);
				if (vlen != str_len(hdr_content) ||
				    strncmp(str_c(hdr_content),
					    (const char *)value, vlen) != 0) {
					if (strlen(str_c(hdr_content)) !=
					    str_len(hdr_content)) {
						/* replace NULs with spaces */
						str_replace_nuls(hdr_content);
					}
					/* store raw */
					data = p_malloc(pool, vlen + 1);
					data[vlen] = '\0';
					header->value = memcpy(data, value, vlen);
					header->value_len = vlen;
					/* store decoded */
					data = p_malloc(pool, str_len(hdr_content) + 1);
					data[str_len(hdr_content)] = '\0';
					header->utf8_value = memcpy(
						data, str_data(hdr_content),
						str_len(hdr_content));
					header->utf8_value_len = str_len(hdr_content);
				} else {
					/* raw == decoded */
					data = p_malloc(pool, vlen + 1);
					data[vlen] = '\0';
					header->value = header->utf8_value =
						memcpy(data, value, vlen);
					header->value_len = header->utf8_value_len = vlen;
				}

				if (hdr_field == _HDR_OTHER)
					continue;
			}

			/* Parse the content type from the Content-type header */
			T_BEGIN {
				switch (hdr_field) {
				case _HDR_CONTENT_TYPE:
					body_part->content_type =
						p_strdup(pool, _parse_content_type(block.hdr));
					break;
				case _HDR_CONTENT_DISPOSITION:
					body_part->content_disposition =
						p_strdup(pool, _parse_content_disposition(block.hdr));
					break;
				default:
					i_unreached();
				}
			} T_END;

			continue;
		}

		/* Reading body */
		if (save_body) {
			(void)message_decoder_decode_next_block(
				decoder, &block, &decoded);
			buffer_append(buf, decoded.data, decoded.size);
		}
	}

	/* Even with an empty message there was at least the "end of headers"
	   block, which set the body_part. */
	i_assert(body_part != NULL);

	/* Save last body part if necessary */
	if (header_part != NULL)
		sieve_message_part_save(renv, buf, header_part, FALSE);
	else if (save_body)
		sieve_message_part_save(renv, buf, body_part, extract_text);

	if (iter_all && !array_is_created(&body_part->headers) &&
	    array_count(&headers) > 0) {
		p_array_init(&body_part->headers, pool, array_count(&headers));
		array_copy(&body_part->headers.arr, 0,
			   &headers.arr, 0, array_count(&headers));
	}

	/* Try to fill the return_body_parts array once more */
	have_all = iter_all ||
		sieve_message_body_get_return_parts(renv, content_types,
						    extract_text);

	/* This time, failure is a bug */
	i_assert(have_all);

	/* Cleanup */
	(void)message_parser_deinit(&parser, &mparts);
	message_decoder_deinit(&decoder);
	buffer_free(&buf);

	/* Return status */
	if (input->stream_errno != 0) {
		sieve_runtime_critical(renv, NULL,
				       "failed to read input message",
				       "read(%s) failed: %s",
				       i_stream_get_name(input),
				       i_stream_get_error(input));
		return SIEVE_EXEC_TEMP_FAILURE;
	}
	return SIEVE_EXEC_OK;
}

int sieve_message_body_get_content(const struct sieve_runtime_env *renv,
				   const char *const *content_types,
				   struct sieve_message_part_data **parts_r)
{
	struct sieve_message_context *msgctx = renv->msgctx;
	int status;

	T_BEGIN {
		/* Fill the return_body_parts array */
		status = sieve_message_parts_add_missing(renv, content_types,
							 FALSE, FALSE);
	} T_END;

	/* Check status */
	if (status <= 0)
		return status;

	/* Return the array of body items */
	(void)array_append_space(&msgctx->return_body_parts); /* NULL-terminate */
	*parts_r = array_idx_modifiable(&msgctx->return_body_parts, 0);

	return status;
}

int sieve_message_body_get_text(const struct sieve_runtime_env *renv,
				struct sieve_message_part_data **parts_r)
{
	static const char *const _text_content_types[] =
		{ "application/xhtml+xml", "text", NULL };
	struct sieve_message_context *msgctx = renv->msgctx;
	int status;

	/* We currently only support extracting plain text from:

	    - text/html -> HTML
	    - application/xhtml+xml -> XHTML

	   Other text types are read as is. Any non-text types are skipped.
	 */

	T_BEGIN {
		/* Fill the return_body_parts array */
		status = sieve_message_parts_add_missing(
			renv, _text_content_types, TRUE, FALSE);
	} T_END;

	/* Check status */
	if (status <= 0)
		return status;

	/* Return the array of body items */
	(void)array_append_space(&msgctx->return_body_parts); /* NULL-terminate */
	*parts_r = array_idx_modifiable(&msgctx->return_body_parts, 0);

	return status;
}

int sieve_message_body_get_raw(const struct sieve_runtime_env *renv,
			       struct sieve_message_part_data **parts_r)
{
	struct sieve_message_context *msgctx = renv->msgctx;
	struct sieve_message_part_data *return_part;
	buffer_t *buf;

	if (msgctx->raw_body == NULL) {
		struct mail *mail = sieve_message_get_mail(renv->msgctx);
		struct istream *input;
		struct message_size hdr_size, body_size;
		const unsigned char *data;
		size_t size;
		int ret;

		msgctx->raw_body = buf =
			buffer_create_dynamic(msgctx->context_pool, 1024*64);

		/* Get stream for message */
 		if (mail_get_stream(mail, &hdr_size, &body_size, &input) < 0) {
			return sieve_runtime_mail_error(
				renv, mail, "failed to open input message");
		}

		/* Skip stream to beginning of body */
		i_stream_skip(input, hdr_size.physical_size);

		/* Read raw message body */
		while ((ret = i_stream_read_more(input, &data, &size)) > 0) {
			buffer_append(buf, data, size);

			i_stream_skip(input, size);
		}

		if (ret < 0 && input->stream_errno != 0) {
			sieve_runtime_critical(
				renv, NULL, "failed to read input message",
				"read(%s) failed: %s",
				i_stream_get_name(input),
				i_stream_get_error(input));
			return SIEVE_EXEC_TEMP_FAILURE;
		}

		/* Add terminating NUL to the body part buffer */
		buffer_append_c(buf, '\0');

	} else {
		buf = msgctx->raw_body;
	}

	/* Clear result array */
	array_clear(&msgctx->return_body_parts);

	if (buf->used > 1) {
		const char *data = (const char *)buf->data;
		size_t size = buf->used - 1;

		i_assert(data[size] == '\0');

		/* Add single item to the result */
		return_part = array_append_space(&msgctx->return_body_parts);
		return_part->content = data;
		return_part->size = size;
	}

	/* Return the array of body items */
	(void)array_append_space(&msgctx->return_body_parts); /* NULL-terminate */
	*parts_r = array_idx_modifiable(&msgctx->return_body_parts, 0);

	return SIEVE_EXEC_OK;
}

/*
 * Message part iterator
 */

int sieve_message_part_iter_init(struct sieve_message_part_iter *iter,
				 const struct sieve_runtime_env *renv)
{
	struct sieve_message_context *msgctx = renv->msgctx;
	struct sieve_message_part *const *parts;
	unsigned int count;
	int status;

	T_BEGIN {
		/* Fill the return_body_parts array */
		status = sieve_message_parts_add_missing(
			renv, NULL, TRUE, TRUE);
	} T_END;

	/* Check status */
	if (status <= 0)
		return status;

	i_zero(iter);
	iter->renv = renv;
	iter->index = 0;
	iter->offset = 0;

	parts = array_get(&msgctx->cached_body_parts, &count);
	if (count == 0)
		iter->root = NULL;
	else
		iter->root = parts[0];

	return SIEVE_EXEC_OK;
}

void sieve_message_part_iter_subtree(struct sieve_message_part_iter *iter,
				     struct sieve_message_part_iter *subtree)
{
	const struct sieve_runtime_env *renv = iter->renv;
	struct sieve_message_context *msgctx = renv->msgctx;
	struct sieve_message_part *const *parts;
	unsigned int count;

	*subtree = *iter;

	parts = array_get(&msgctx->cached_body_parts, &count);
	if (subtree->index >= count)
		subtree->root = NULL;
	else
		subtree->root = parts[subtree->index];
	subtree->offset = subtree->index;
}

void sieve_message_part_iter_children(struct sieve_message_part_iter *iter,
				      struct sieve_message_part_iter *child)
{
	const struct sieve_runtime_env *renv = iter->renv;
	struct sieve_message_context *msgctx = renv->msgctx;
	struct sieve_message_part *const *parts;
	unsigned int count;

	*child = *iter;

	parts = array_get(&msgctx->cached_body_parts, &count);
	if ((child->index+1) >= count || parts[child->index]->children == NULL)
		child->root = NULL;
	else
		child->root = parts[child->index++];
	child->offset = child->index;
}

struct sieve_message_part *
sieve_message_part_iter_current(struct sieve_message_part_iter *iter)
{
	const struct sieve_runtime_env *renv = iter->renv;
	struct sieve_message_context *msgctx = renv->msgctx;
	struct sieve_message_part *const *parts;
	unsigned int count;

	if (iter->root == NULL)
		return NULL;

	parts = array_get(&msgctx->cached_body_parts, &count);
	if (iter->index >= count)
		return NULL;
	do {
		if (parts[iter->index] == iter->root->next)
			return NULL;
		if (parts[iter->index] == iter->root->parent)
			return NULL;
	} while (parts[iter->index]->epilogue && ++iter->index < count);
	if (iter->index >= count)
		return NULL;
	return parts[iter->index];
}

struct sieve_message_part *
sieve_message_part_iter_next(struct sieve_message_part_iter *iter)
{
	const struct sieve_runtime_env *renv = iter->renv;
	struct sieve_message_context *msgctx = renv->msgctx;

	if (iter->index >= array_count(&msgctx->cached_body_parts))
		return NULL;
	iter->index++;

	return sieve_message_part_iter_current(iter);
}

void sieve_message_part_iter_reset(struct sieve_message_part_iter *iter)
{
	iter->index = iter->offset;
}

/*
 * MIME header list
 */

/* Forward declarations */

static int
sieve_mime_header_list_next_item(struct sieve_header_list *_hdrlist,
				 const char **name_r, string_t **value_r);
static int
sieve_mime_header_list_next_value(struct sieve_stringlist *_strlist,
				  string_t **value_r);
static void sieve_mime_header_list_reset(struct sieve_stringlist *_strlist);

/* Header list object */

struct sieve_mime_header_list {
	struct sieve_header_list hdrlist;

	struct sieve_stringlist *field_names;

	struct sieve_message_part_iter part_iter;

	const char *header_name;
	const struct sieve_message_header *headers;
	unsigned int headers_index, headers_count;

	bool mime_decode:1;
	bool children:1;
};

struct sieve_header_list *
sieve_mime_header_list_create(const struct sieve_runtime_env *renv,
			      struct sieve_stringlist *field_names,
			      struct sieve_message_part_iter *part_iter,
			      bool mime_decode, bool children)
{
	struct sieve_mime_header_list *hdrlist;

	hdrlist = t_new(struct sieve_mime_header_list, 1);
	hdrlist->hdrlist.strlist.runenv = renv;
	hdrlist->hdrlist.strlist.exec_status = SIEVE_EXEC_OK;
	hdrlist->hdrlist.strlist.next_item = sieve_mime_header_list_next_value;
	hdrlist->hdrlist.strlist.reset = sieve_mime_header_list_reset;
	hdrlist->hdrlist.next_item = sieve_mime_header_list_next_item;
	hdrlist->field_names = field_names;
	hdrlist->mime_decode = mime_decode;
	hdrlist->children = children;

	sieve_message_part_iter_subtree(part_iter, &hdrlist->part_iter);

	return &hdrlist->hdrlist;
}

/* MIME list implementation */

static void
sieve_mime_header_list_next_name(struct sieve_mime_header_list *hdrlist)
{
	struct sieve_message_part *mpart;

	sieve_message_part_iter_reset(&hdrlist->part_iter);
	mpart = sieve_message_part_iter_current(&hdrlist->part_iter);

	if (mpart != NULL && array_is_created(&mpart->headers)) {
		hdrlist->headers = array_get(&mpart->headers,
					     &hdrlist->headers_count);
		hdrlist->headers_index = 0;
	}
}

static int
sieve_mime_header_list_next_item(struct sieve_header_list *_hdrlist,
				 const char **name_r, string_t **value_r)
{
	struct sieve_mime_header_list *hdrlist =
		(struct sieve_mime_header_list *)_hdrlist;
	const struct sieve_runtime_env *renv = _hdrlist->strlist.runenv;

	if (name_r != NULL)
		*name_r = NULL;
	*value_r = NULL;

	for (;;) {
		/* Check for end of current header list */
		if (hdrlist->headers_count == 0 ||
		    hdrlist->headers_index >= hdrlist->headers_count) {
			hdrlist->headers_count = 0;
			hdrlist->headers_index = 0;
			hdrlist->headers = NULL;
		}

		/* Fetch more headers */
		while (hdrlist->headers_count == 0) {
			string_t *hdr_item = NULL;
			int ret;

			if (hdrlist->header_name != NULL && hdrlist->children) {
				struct sieve_message_part *mpart;

				mpart = sieve_message_part_iter_next(
					&hdrlist->part_iter);
				if (mpart != NULL &&
				    array_is_created(&mpart->headers)) {
					hdrlist->headers = array_get(
						&mpart->headers,
						&hdrlist->headers_count);
					hdrlist->headers_index = 0;
				}
				if (hdrlist->headers_count > 0) {
					if (_hdrlist->strlist.trace) {
						sieve_runtime_trace(
							renv, 0,
							"moving to next message part");
					}
					break;
				}
			}

			/* Read next header name from source list */
			if ((ret = sieve_stringlist_next_item(
				hdrlist->field_names, &hdr_item)) <= 0)
				return ret;

			hdrlist->header_name = str_c(hdr_item);

			if (_hdrlist->strlist.trace) {
				sieve_runtime_trace(
					renv, 0,
					"extracting '%s' headers from message part",
					str_sanitize(str_c(hdr_item), 80));
			}

			sieve_mime_header_list_next_name(hdrlist);
		}

		for (; hdrlist->headers_index < hdrlist->headers_count;
		     hdrlist->headers_index++) {
			const struct sieve_message_header *header =
				&hdrlist->headers[hdrlist->headers_index];

			if (strcasecmp(header->name, hdrlist->header_name) == 0) {
				if (name_r != NULL)
					*name_r = hdrlist->header_name;
				if (hdrlist->mime_decode) {
					*value_r = t_str_new_const(
						(const char *)header->utf8_value,
						header->utf8_value_len);
				} else {
					*value_r = t_str_new_const(
						(const char *)header->value,
						header->value_len);
				}
				hdrlist->headers_index++;
				return 1;
			}
		}
	}

	i_unreached();
}

static int
sieve_mime_header_list_next_value(struct sieve_stringlist *_strlist,
				  string_t **value_r)
{
	struct sieve_header_list *hdrlist =
		(struct sieve_header_list *)_strlist;

	return sieve_mime_header_list_next_item(hdrlist, NULL, value_r);
}

static void
sieve_mime_header_list_reset(struct sieve_stringlist *strlist)
{
	struct sieve_mime_header_list *hdrlist =
		(struct sieve_mime_header_list *)strlist;

	sieve_stringlist_reset(hdrlist->field_names);
	hdrlist->header_name = NULL;
}
