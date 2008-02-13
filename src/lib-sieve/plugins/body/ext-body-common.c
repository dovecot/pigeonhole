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

#include "sieve-common.h"
#include "sieve-interpreter.h"

#include "ext-body-common.h"

/* This implementation is largely borrowed from the original sieve-cmu.c of the 
 * cmusieve plugin.
 */
 
struct ext_body_part_cached {
	const char *content_type;

	const char *raw_body;
	const char *decoded_body;
	size_t raw_body_size;
	size_t decoded_body_size;
	bool have_body; /* there's the empty end-of-headers line */
};

struct ext_body_message_context {
	pool_t pool;
	ARRAY_DEFINE(cached_body_parts, struct ext_body_part_cached);
	ARRAY_DEFINE(return_body_parts, struct ext_body_part);
	buffer_t *tmp_buffer;
};

static bool _is_wanted_content_type
(const char * const *wanted_types, const char *content_type)
{
	const char *subtype = strchr(content_type, '/');
	size_t type_len;

	type_len = subtype == NULL ? strlen(content_type) :
		(size_t)(subtype - content_type);

	for (; *wanted_types != NULL; wanted_types++) {
		const char *wanted_subtype = strchr(*wanted_types, '/');

		if (**wanted_types == '\0') {
			/* empty string matches everything */
			return TRUE;
		}
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

static bool ext_body_get_return_parts
(struct ext_body_message_context *ctx, const char * const *wanted_types,
	bool decode_to_plain)
{
	const struct ext_body_part_cached *body_parts;
	unsigned int i, count;
	struct ext_body_part *return_part;

	body_parts = array_get(&ctx->cached_body_parts, &count);
	if (count == 0)
		return FALSE;

	array_clear(&ctx->return_body_parts);
	for (i = 0; i < count; i++) {
		if (!body_parts[i].have_body) {
			/* doesn't match anything */
			continue;
		}

		if (!_is_wanted_content_type(wanted_types, body_parts[i].content_type))
			continue;

		return_part = array_append_space(&ctx->return_body_parts);
		if (decode_to_plain) {
			if (body_parts[i].decoded_body == NULL)
				return FALSE;
			return_part->content = body_parts[i].decoded_body;
			return_part->size = body_parts[i].decoded_body_size;
		} else {
			if (body_parts[i].raw_body == NULL)
				return FALSE;
			return_part->content = body_parts[i].raw_body;
			return_part->size = body_parts[i].raw_body_size;
		}
	}
	return TRUE;
}

static void ext_body_part_save
(struct ext_body_message_context *ctx, struct message_part *part,
	struct ext_body_part_cached *body_part, bool decoded)
{
	buffer_t *buf = ctx->tmp_buffer;

	buffer_append_c(buf, '\0');
	if ( !decoded ) {
		body_part->raw_body = p_strdup(ctx->pool, buf->data);
		body_part->raw_body_size = buf->used - 1;
		i_assert(buf->used - 1 == part->body_size.physical_size);
	} else {
		body_part->decoded_body = p_strdup(ctx->pool, buf->data);
		body_part->decoded_body_size = buf->used - 1;
	}
	buffer_set_used_size(buf, 0);
}

static const char *_parse_content_type(const struct message_header_line *hdr)
{
	struct rfc822_parser_context parser;
	string_t *content_type;

	rfc822_parser_init(&parser, hdr->full_value, hdr->full_value_len, NULL);
	(void)rfc822_skip_lwsp(&parser);

	content_type = t_str_new(64);
	if (rfc822_parse_content_type(&parser, content_type) < 0)
		return "";
	return str_c(content_type);
}

static bool ext_body_parts_add_missing
(const struct sieve_message_data *msgdata, struct ext_body_message_context *ctx, 
	const char * const *content_types, bool decode_to_plain)
{
	struct ext_body_part_cached *body_part = NULL;
	struct message_parser_ctx *parser;
	struct message_decoder_context *decoder;
	struct message_block block, decoded;
	const struct message_part *const_parts;
	struct message_part *parts, *prev_part = NULL;
	struct istream *input;
	unsigned int idx = 0;
	bool save_body = FALSE, have_all;
	int ret;

	if (ext_body_get_return_parts(ctx, content_types, decode_to_plain))
		return TRUE;

	if (mail_get_stream(msgdata->mail, NULL, NULL, &input) < 0)
		return FALSE;
	if (mail_get_parts(msgdata->mail, &const_parts) < 0)
		return FALSE;
	parts = (struct message_part *)const_parts;

	buffer_set_used_size(ctx->tmp_buffer, 0);
	decoder = decode_to_plain ? message_decoder_init(FALSE) : NULL;
	parser = message_parser_init_from_parts(parts, input, 0, 0);
	while ((ret = message_parser_parse_next_block(parser, &block)) > 0) {
		if (block.part != prev_part) {
			if (body_part != NULL && save_body) {
				ext_body_part_save(ctx, prev_part, body_part, decoder != NULL);
			}
			prev_part = block.part;
			body_part = array_idx_modifiable(&ctx->cached_body_parts, idx);
			idx++;
			body_part->content_type = "text/plain";
		}
		if (block.hdr != NULL || block.size == 0) {
			/* reading headers */
			if (decoder != NULL) {
				(void)message_decoder_decode_next_block(decoder,
					&block, &decoded);
			}

			if (block.hdr == NULL) {
				/* save bodies only if we have a wanted
				   content-type */
				save_body = _is_wanted_content_type
					(content_types, body_part->content_type);
				continue;
			}
			
			if (block.hdr->eoh)
				body_part->have_body = TRUE;
				
			/* We're interested of only Content-Type: header */
			if (strcasecmp(block.hdr->name, "Content-Type") != 0)
				continue;

			if (block.hdr->continues) {
				block.hdr->use_full_value = TRUE;
				continue;
			}
		
			T_BEGIN {
				body_part->content_type =
					p_strdup(ctx->pool, _parse_content_type(block.hdr));
			} T_END;
			continue;
		}

		/* reading body */
		if (save_body) {
			if (decoder != NULL) {
				(void)message_decoder_decode_next_block(decoder,
							&block, &decoded);
				buffer_append(ctx->tmp_buffer,
					      decoded.data, decoded.size);
			} else {
				buffer_append(ctx->tmp_buffer,
					      block.data, block.size);
			}
		}
	}

	if (body_part != NULL && save_body)
		ext_body_part_save(ctx, prev_part, body_part, decoder != NULL);

	have_all = ext_body_get_return_parts(ctx, content_types, decode_to_plain);
	i_assert(have_all);

	(void)message_parser_deinit(&parser);
	if (decoder != NULL)
		message_decoder_deinit(&decoder);
	return ( input->stream_errno == 0 );
}

static struct ext_body_message_context *ext_body_get_context
(struct sieve_message_context *msgctx)
{
	pool_t pool = sieve_message_context_pool(msgctx);
	struct ext_body_message_context *ctx;
	
	ctx = (struct ext_body_message_context *)
		sieve_message_context_extension_get(msgctx, ext_body_my_id);
	
	if ( ctx == NULL ) {
		ctx = p_new(pool, struct ext_body_message_context, 1);	
		ctx->pool = pool;
		p_array_init(&ctx->cached_body_parts, pool, 8);
		p_array_init(&ctx->return_body_parts, pool, 8);
		ctx->tmp_buffer = buffer_create_dynamic(pool, 1024*64);
		
		sieve_message_context_extension_set
			(msgctx, ext_body_my_id, (void *) ctx);
	}
	
	return ctx;
}

bool ext_body_get_content
(const struct sieve_runtime_env *renv, const char * const *content_types,
	int decode_to_plain, struct ext_body_part **parts_r)
{
	bool result = TRUE;
	struct ext_body_message_context *ctx = ext_body_get_context(renv->msgctx);

	T_BEGIN {
		if ( !ext_body_parts_add_missing
			(renv->msgdata, ctx, content_types, decode_to_plain != 0) )
			result = FALSE;
	} T_END;
	
	if ( !result ) return FALSE;

	(void)array_append_space(&ctx->return_body_parts); /* NULL-terminate */
	*parts_r = array_idx_modifiable(&ctx->return_body_parts, 0);

	return result;
}
