/* Copyright (c) 2002-2015 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "buffer.h"
#include "array.h"
#include "str.h"
#include "istream.h"
#include "rfc822-parser.h"
#include "message-date.h"
#include "message-parser.h"
#include "message-decoder.h"
#include "mail-html2text.h"
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-interpreter.h"

#include "ext-body-common.h"

struct ext_body_part {
	const char *content;
	unsigned long size;
};

struct ext_body_part_cached {
	const char *content_type;

	const char *decoded_body;
	const char *text_body;
	size_t decoded_body_size;
	size_t text_body_size;

	bool have_body; /* there's the empty end-of-headers line */
};

struct ext_body_message_context {
	pool_t pool;
	ARRAY(struct ext_body_part_cached) cached_body_parts;
	ARRAY(struct ext_body_part) return_body_parts;
	buffer_t *tmp_buffer;
	buffer_t *raw_body;
};

static bool _is_wanted_content_type
(const char * const *wanted_types, const char *content_type)
{
	const char *subtype = strchr(content_type, '/');
	size_t type_len;

	type_len = ( subtype == NULL ? strlen(content_type) :
		(size_t)(subtype - content_type) );

	i_assert( wanted_types != NULL );

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
			  strncasecmp(*wanted_types, content_type, type_len) == 0)
				return TRUE;
		} else {
			/* match whole type/subtype */
			if (strcasecmp(*wanted_types, content_type) == 0)
				return TRUE;
		}
	}
	return FALSE;
}

static bool _want_multipart_content_type
(const char * const *wanted_types)
{
	for (; *wanted_types != NULL; wanted_types++) {
		if (**wanted_types == '\0') {
			/* empty string matches everything */
			return TRUE;
		}

		/* match only main type */
		if ( strncasecmp(*wanted_types, "multipart", 9) == 0 &&
			( strlen(*wanted_types) == 9 || *(*wanted_types+9) == '/' ) )
			return TRUE;
	}

	return FALSE;
}


static bool ext_body_get_return_parts
(struct ext_body_message_context *ctx, const char * const *wanted_types,
	bool extract_text)
{
	const struct ext_body_part_cached *body_parts;
	unsigned int i, count;
	struct ext_body_part *return_part;

	/* Check whether any body parts are cached already */
	body_parts = array_get(&ctx->cached_body_parts, &count);
	if ( count == 0 )
		return FALSE;

	/* Clear result array */
	array_clear(&ctx->return_body_parts);

	/* Fill result array with requested content_types */
	for (i = 0; i < count; i++) {
		if (!body_parts[i].have_body) {
			/* Part has no body; according to RFC this MUST not match to anything and
			 * therefore it is not included in the result.
			 */
			continue;
		}

		/* Skip content types that are not requested */
		if (!_is_wanted_content_type(wanted_types, body_parts[i].content_type))
			continue;

		/* Add new item to the result */
		return_part = array_append_space(&ctx->return_body_parts);

		/* Depending on whether a decoded body part is requested, the appropriate
		 * cache item is read. If it is missing, this function fails and the cache
		 * needs to be completed by ext_body_parts_add_missing().
		 */
		if (extract_text) {
			if (body_parts[i].text_body == NULL)
				return FALSE;
			return_part->content = body_parts[i].text_body;
			return_part->size = body_parts[i].text_body_size;
		} else {
			if (body_parts[i].decoded_body == NULL)
				return FALSE;
			return_part->content = body_parts[i].decoded_body;
			return_part->size = body_parts[i].decoded_body_size;			
		}
	}

	return TRUE;
}

static void ext_body_part_save
(struct ext_body_message_context *ctx,
	struct ext_body_part_cached *body_part, bool extract_text)
{
	buffer_t *buf = ctx->tmp_buffer;
	buffer_t *text_buf = NULL;
	char *part_data;
	size_t part_size;

	/* Add terminating NUL to the body part buffer */
	buffer_append_c(buf, '\0');

	if ( extract_text ) {
		if ( mail_html2text_content_type_match
			(body_part->content_type) ) {
			struct mail_html2text *html2text;

			text_buf = buffer_create_dynamic(default_pool, 4096);

			/* Remove HTML markup */
			html2text = mail_html2text_init(0);
			mail_html2text_more(html2text, buf->data, buf->used, text_buf);
			mail_html2text_deinit(&html2text);
	
			buf = text_buf;
		}
	}

	part_data = p_malloc(ctx->pool, buf->used);
	memcpy(part_data, buf->data, buf->used);
	part_size = buf->used - 1;

	if ( text_buf != NULL)
		buffer_free(&text_buf);

	/* Depending on whether the part is processed into text, store message
	 * body in the appropriate cache location.
	 */
	if ( !extract_text ) {
		body_part->decoded_body = part_data;
		body_part->decoded_body_size = part_size;
	} else {
		body_part->text_body = part_data;
		body_part->text_body_size = part_size;
	}

	/* Clear buffer */
	buffer_set_used_size(ctx->tmp_buffer, 0);
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

	/* Content-type value must end here, otherwise it is invalid after all */
	(void)rfc822_skip_lwsp(&parser);
	if ( parser.data != parser.end && *parser.data != ';' )
		return "";

	/* Success */
	return str_c(content_type);
}

/* ext_body_parts_add_missing():
 *   Add requested message body parts to the cache that are missing.
 */
static int ext_body_parts_add_missing
(const struct sieve_runtime_env *renv,
	struct ext_body_message_context *ctx,
	const char *const *content_types, bool extract_text)
{
	buffer_t *buf = ctx->tmp_buffer;
	struct mail *mail = sieve_message_get_mail(renv->msgctx);
	struct ext_body_part_cached *body_part = NULL, *header_part = NULL;
	struct message_parser_ctx *parser;
	struct message_decoder_context *decoder;
	struct message_block block, decoded;
	struct message_part *parts, *prev_part = NULL;
	ARRAY(struct message_part *) part_index;
	struct istream *input;
	unsigned int idx = 0;
	bool save_body = FALSE, want_multipart, have_all;
	int ret;

	/* First check whether any are missing */
	if (ext_body_get_return_parts(ctx, content_types, extract_text)) {
		/* Cache hit; all are present */
		return SIEVE_EXEC_OK;
	}

	/* Get the message stream */
	if ( mail_get_stream(mail, NULL, NULL, &input) < 0 ) {
		return sieve_runtime_mail_error(renv, mail,
			"body test: failed to read input message");
	}
	if (mail_get_parts(mail, &parts) < 0) {
		return sieve_runtime_mail_error(renv, mail,
			"body test: failed to parse input message");
	}

	if ( (want_multipart=_want_multipart_content_type(content_types)) ) {
		t_array_init(&part_index, 8);
	}

	buffer_set_used_size(buf, 0);

	/* Initialize body decoder */
	decoder = message_decoder_init(NULL, 0);

	//parser = message_parser_init_from_parts(parts, input, 0,
		//MESSAGE_PARSER_FLAG_INCLUDE_MULTIPART_BLOCKS);
	parser = message_parser_init(ctx->pool, input, 0,
		MESSAGE_PARSER_FLAG_INCLUDE_MULTIPART_BLOCKS);
	while ( (ret = message_parser_parse_next_block(parser, &block)) > 0 ) {

		if ( block.part != prev_part ) {
			bool message_rfc822 = FALSE;

			/* Save previous body part */
			if ( body_part != NULL ) {
				/* Treat message/rfc822 separately; headers become content */
				if ( block.part->parent == prev_part &&
					strcmp(body_part->content_type, "message/rfc822") == 0 ) {
					message_rfc822 = TRUE;
				} else {
					if ( save_body ) {
						ext_body_part_save(ctx, body_part, extract_text);
					}
				}
			}

			/* Start processing next */
			body_part = array_idx_modifiable(&ctx->cached_body_parts, idx);
			body_part->content_type = "text/plain";

			/* Check whether this is the epilogue block of a wanted multipart part */
			if ( want_multipart ) {
				array_idx_set(&part_index, idx, &block.part);

				if ( prev_part != NULL && prev_part->next != block.part &&
					block.part->parent != prev_part ) {
					struct message_part *const *iparts;
					unsigned int count, i;

					iparts = array_get(&part_index, &count);
					for ( i = 0; i < count; i++ ) {
						if ( iparts[i] == block.part ) {
							const struct ext_body_part_cached *parent =
								array_idx(&ctx->cached_body_parts, i);
							body_part->content_type = parent->content_type;
							body_part->have_body = TRUE;
							save_body = _is_wanted_content_type
								(content_types, body_part->content_type);
							break;
						}
					}
				}
			}

			/* If this is message/rfc822 content retain the enveloping part for
			 * storing headers as content.
			 */
			if ( message_rfc822 ) {
				i_assert(idx > 0);
				header_part = array_idx_modifiable(&ctx->cached_body_parts, idx-1);
			} else {
				header_part = NULL;
			}

			prev_part = block.part;
			idx++;
		}

		if ( block.hdr != NULL || block.size == 0 ) {
			/* Reading headers */

			/* Decode block */
			(void)message_decoder_decode_next_block(decoder, &block, &decoded);

			/* Check for end of headers */
			if ( block.hdr == NULL ) {
				/* Save headers for message/rfc822 part */
				if ( header_part != NULL ) {
					ext_body_part_save(ctx, header_part, extract_text);
					header_part = NULL;
				}

				/* Save bodies only if we have a wanted content-type */
				i_assert( body_part != NULL );
				save_body = _is_wanted_content_type
					(content_types, body_part->content_type);
				continue;
			}

			/* Encountered the empty line that indicates the end of the headers and
			 * the start of the body
			 */
			if ( block.hdr->eoh ) {
				i_assert( body_part != NULL );
				body_part->have_body = TRUE;
			} else if ( header_part != NULL ) {
				/* Save message/rfc822 header as part content */
				if ( block.hdr->continued ) {
					buffer_append(buf, block.hdr->value, block.hdr->value_len);
				} else {
					buffer_append(buf, block.hdr->name, block.hdr->name_len);
					buffer_append(buf, block.hdr->middle, block.hdr->middle_len);
					buffer_append(buf, block.hdr->value, block.hdr->value_len);
				}
				if ( !block.hdr->no_newline ) {
					buffer_append(buf, "\r\n", 2);
				}
			}

			/* We're interested of only Content-Type: header */
			if ( strcasecmp(block.hdr->name, "Content-Type" ) != 0 )
				continue;

			/* Header can have folding whitespace. Acquire the full value before
			 * continuing
			 */
			if ( block.hdr->continues ) {
				block.hdr->use_full_value = TRUE;
				continue;
			}

			i_assert( body_part != NULL );

			/* Parse the content type from the Content-type header */
			T_BEGIN {
				body_part->content_type =
					p_strdup(ctx->pool, _parse_content_type(block.hdr));
			} T_END;

			continue;
		}

		/* Reading body */
		if ( save_body ) {
			(void)message_decoder_decode_next_block(decoder, &block, &decoded);
			buffer_append(buf, decoded.data, decoded.size);
		}
	}

	/* Save last body part if necessary */
	if ( header_part != NULL ) {
		ext_body_part_save(ctx, header_part, FALSE);
	} else if ( body_part != NULL && save_body ) {
		ext_body_part_save(ctx, body_part, extract_text);
	}

	/* Try to fill the return_body_parts array once more */
	have_all = ext_body_get_return_parts(ctx, content_types, extract_text);

	/* This time, failure is a bug */
	i_assert(have_all);

	/* Cleanup */
	(void)message_parser_deinit(&parser, &parts);
	message_decoder_deinit(&decoder);

	/* Return status */
	if ( input->stream_errno != 0 ) {
		sieve_runtime_critical(renv, NULL,
			"body test: failed to read input message",
			"body test: failed to read message stream: %s",
			i_stream_get_error(input));
		return SIEVE_EXEC_TEMP_FAILURE;
	}
	return SIEVE_EXEC_OK;
}

static struct ext_body_message_context *ext_body_get_context
(const struct sieve_extension *this_ext, struct sieve_message_context *msgctx)
{
	struct ext_body_message_context *ctx;

	/* Get message context (contains cached message body information) */
	ctx = (struct ext_body_message_context *)
		sieve_message_context_extension_get(msgctx, this_ext);

	/* Create it if it does not exist already */
	if ( ctx == NULL ) {
		pool_t pool;

		pool = sieve_message_context_pool(msgctx);
		ctx = p_new(pool, struct ext_body_message_context, 1);
		ctx->pool = pool;

		p_array_init(&ctx->cached_body_parts, pool, 8);
		p_array_init(&ctx->return_body_parts, pool, 8);
		ctx->tmp_buffer = buffer_create_dynamic(pool, 1024*64);
		ctx->raw_body = NULL;

		/* Register context */
		sieve_message_context_extension_set(msgctx, this_ext, (void *) ctx);
	}

	return ctx;
}

static int ext_body_get_content
(const struct sieve_runtime_env *renv, const char * const *content_types,
	struct ext_body_part **parts_r)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct ext_body_message_context *ctx =
		ext_body_get_context(this_ext, renv->msgctx);
	int status;

	T_BEGIN {
		/* Fill the return_body_parts array */
		status = ext_body_parts_add_missing
			(renv, ctx, content_types, FALSE);
	} T_END;

	/* Check status */
	if ( status <= 0 )
		return status;

	/* Return the array of body items */
	(void) array_append_space(&ctx->return_body_parts); /* NULL-terminate */
	*parts_r = array_idx_modifiable(&ctx->return_body_parts, 0);

	return status;
}

static int ext_body_get_text
(const struct sieve_runtime_env *renv, struct ext_body_part **parts_r)
{
	/* We currently only support extracting plain text from:

	    - text/html -> HTML
	    - application/xhtml+xml -> XHTML

	   Other text types are read as is. Any non-text types are skipped.
	 */
	static const char * const _text_content_types[] =
		{ "application/xhtml+xml", "text", NULL };
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct ext_body_message_context *ctx =
		ext_body_get_context(this_ext, renv->msgctx);
	int status;

	T_BEGIN {
		/* Fill the return_body_parts array */
		status = ext_body_parts_add_missing
			(renv, ctx, _text_content_types, TRUE);
	} T_END;

	/* Check status */
	if ( status <= 0 )
		return status;

	/* Return the array of body items */
	(void) array_append_space(&ctx->return_body_parts); /* NULL-terminate */
	*parts_r = array_idx_modifiable(&ctx->return_body_parts, 0);

	return status;
}

static int ext_body_get_raw
(const struct sieve_runtime_env *renv, struct ext_body_part **parts_r)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct ext_body_message_context *ctx =
		ext_body_get_context(this_ext, renv->msgctx);
	struct ext_body_part *return_part;
	buffer_t *buf;

	if ( ctx->raw_body == NULL ) {
		struct mail *mail = sieve_message_get_mail(renv->msgctx);
		struct istream *input;
		struct message_size hdr_size, body_size;
		const unsigned char *data;
		size_t size;
		int ret;

		ctx->raw_body = buf = buffer_create_dynamic(ctx->pool, 1024*64);

		/* Get stream for message */
 		if ( mail_get_stream(mail, &hdr_size, &body_size, &input) < 0 ) {
			return sieve_runtime_mail_error(renv, mail,
				"body test: failed to read input message");
		}

		/* Skip stream to beginning of body */
		i_stream_skip(input, hdr_size.physical_size);

		/* Read raw message body */
		while ( (ret=i_stream_read_data(input, &data, &size, 0)) > 0 ) {
			buffer_append(buf, data, size);

			i_stream_skip(input, size);
		}

		if ( ret == -1 && input->stream_errno != 0 ) {
			sieve_runtime_critical(renv, NULL,
				"body test: failed to read input message as raw",
				"body test: failed to read raw message stream: %s",
				i_stream_get_error(input));
			return SIEVE_EXEC_TEMP_FAILURE;
		}
	} else {
		buf = ctx->raw_body;
	}

	/* Clear result array */
	array_clear(&ctx->return_body_parts);

	if ( buf->used > 0  ) {
		/* Add terminating NUL to the body part buffer */
		buffer_append_c(buf, '\0');

		/* Add single item to the result */
		return_part = array_append_space(&ctx->return_body_parts);
		return_part->content = buf->data;
		return_part->size = buf->used - 1;
	}

	/* Return the array of body items */
	(void) array_append_space(&ctx->return_body_parts); /* NULL-terminate */
	*parts_r = array_idx_modifiable(&ctx->return_body_parts, 0);

	return SIEVE_EXEC_OK;
}

/*
 * Body part stringlist
 */

static int ext_body_stringlist_next_item
	(struct sieve_stringlist *_strlist, string_t **str_r);
static void ext_body_stringlist_reset
	(struct sieve_stringlist *_strlist);

struct ext_body_stringlist {
	struct sieve_stringlist strlist;

	struct ext_body_part *body_parts;
	struct ext_body_part *body_parts_iter;
};

int ext_body_get_part_list
(const struct sieve_runtime_env *renv, enum tst_body_transform transform,
	const char * const *content_types, struct sieve_stringlist **strlist_r)
{
	static const char * const _no_content_types[] = { "", NULL };
	struct ext_body_stringlist *strlist;
	struct ext_body_part *body_parts = NULL;
	int ret;

	*strlist_r = NULL;

	if ( content_types == NULL ) content_types = _no_content_types;

	switch ( transform ) {
	case TST_BODY_TRANSFORM_RAW:
		if ( (ret=ext_body_get_raw(renv, &body_parts)) <= 0 )
			return ret;
		break;
	case TST_BODY_TRANSFORM_CONTENT:
		if ( (ret=ext_body_get_content
			(renv, content_types, &body_parts)) <= 0 )
			return ret;
		break;
	case TST_BODY_TRANSFORM_TEXT:
		if ( (ret=ext_body_get_text(renv, &body_parts)) <= 0 )
			return ret;
		break;
	default:
		i_unreached();
	}

	strlist = t_new(struct ext_body_stringlist, 1);
	strlist->strlist.runenv = renv;
	strlist->strlist.next_item = ext_body_stringlist_next_item;
	strlist->strlist.reset = ext_body_stringlist_reset;
	strlist->body_parts = body_parts;
	strlist->body_parts_iter = body_parts;

	*strlist_r = &strlist->strlist;
	return SIEVE_EXEC_OK;
}

static int ext_body_stringlist_next_item
(struct sieve_stringlist *_strlist, string_t **str_r)
{
	struct ext_body_stringlist *strlist =
		(struct ext_body_stringlist *)_strlist;

	*str_r = NULL;

	if ( strlist->body_parts_iter->content == NULL ) return 0;

	*str_r = t_str_new_const
		(strlist->body_parts_iter->content, strlist->body_parts_iter->size);
	strlist->body_parts_iter++;
	return 1;
}

static void ext_body_stringlist_reset
(struct sieve_stringlist *_strlist)
{
	struct ext_body_stringlist *strlist =
		(struct ext_body_stringlist *)_strlist;

	strlist->body_parts_iter = strlist->body_parts;
}
