/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "rfc822-parser.h"
#include "rfc2231-parser.h"
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-binary.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-result.h"
#include "sieve-validator.h"
#include "sieve-generator.h"

#include "ext-mime-common.h"

/*
 * Tagged argument
 */

static bool tag_mime_validate
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);
static bool tag_mime_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
    struct sieve_command *context);

const struct sieve_argument_def mime_tag = {
	.identifier = "mime",
	.validate = tag_mime_validate,
	.generate = tag_mime_generate
};

static bool tag_mime_option_validate
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);

const struct sieve_argument_def mime_anychild_tag = {
	.identifier = "anychild",
	.validate = tag_mime_option_validate
};

const struct sieve_argument_def mime_type_tag = {
	.identifier = "type",
	.validate = tag_mime_option_validate
};

const struct sieve_argument_def mime_subtype_tag = {
	.identifier = "subtype",
	.validate = tag_mime_option_validate
};

const struct sieve_argument_def mime_contenttype_tag = {
	.identifier = "contenttype",
	.validate = tag_mime_option_validate
};

const struct sieve_argument_def mime_param_tag = {
	.identifier = "param",
	.validate = tag_mime_option_validate
};

/*
 * Header override
 */

static bool svmo_mime_dump_context
	(const struct sieve_message_override *svmo,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int svmo_mime_read_context
	(const struct sieve_message_override *svmo,
		const struct sieve_runtime_env *renv, sieve_size_t *address,
		void **ho_context);
static int svmo_mime_header_override
	(const struct sieve_message_override *svmo,
		const struct sieve_runtime_env *renv,
		bool mime_decode, struct sieve_stringlist **headers);

const struct sieve_message_override_def mime_header_override = {
	SIEVE_OBJECT("mime", &mime_operand, 0),
	.sequence = 0, /* Completely replace header source */
	.dump_context = svmo_mime_dump_context,
	.read_context = svmo_mime_read_context,
	.header_override = svmo_mime_header_override
};

/*
 * Operand
 */

static const struct sieve_extension_objects ext_header_overrides =
	SIEVE_EXT_DEFINE_MESSAGE_OVERRIDE(mime_header_override);

const struct sieve_operand_def mime_operand = {
	.name = "mime operand",
	.ext_def = &mime_extension,
	.class = &sieve_message_override_operand_class,
	.interface = &ext_header_overrides
};

/*
 * Tag data
 */

struct tag_mime_data {
	enum ext_mime_option mimeopt;
	struct sieve_ast_argument *param_arg;
	unsigned int anychild:1;
};

/*
 * Tag validation
 */

static struct tag_mime_data *
tag_mime_get_data(struct sieve_command *cmd,
	struct sieve_ast_argument *tag)
{
	struct tag_mime_data *data;

	if (tag->argument->data == NULL) {
		data = p_new(sieve_command_pool(cmd), struct tag_mime_data, 1);
		tag->argument->data = (void *)data;
	} else {
		data = (struct tag_mime_data *)tag->argument->data;
	}

	return data;
}

static bool tag_mime_validate
(struct sieve_validator *valdtr ATTR_UNUSED,
	struct sieve_ast_argument **arg, struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Skip the tag itself */
	*arg = sieve_ast_argument_next(*arg);

	(void)tag_mime_get_data(cmd, tag);
	return TRUE;
}

static bool tag_mime_option_validate
(struct sieve_validator *valdtr ATTR_UNUSED,
	struct sieve_ast_argument **arg, struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;
	struct sieve_ast_argument *mime_arg;
	struct tag_mime_data *data;

	i_assert(tag != NULL);

	/* Detach tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Find required ":mime" tag */
	mime_arg = sieve_command_find_argument(cmd, &mime_tag);
	if ( mime_arg == NULL ) {
		sieve_argument_validate_error(valdtr, tag,
			"the :%s tag for the %s %s cannot be specified "
			"without the :mime tag", sieve_ast_argument_tag(tag),
			sieve_command_identifier(cmd), sieve_command_type_name(cmd));
		return FALSE;
	}

	/* Annotate ":mime" tag with the data provided by this option tag */
	data = tag_mime_get_data(cmd, mime_arg);
	if ( sieve_argument_is(tag, mime_anychild_tag) )
		data->anychild = TRUE;
	else 	{
		if ( data->mimeopt != EXT_MIME_OPTION_NONE ) {
			sieve_argument_validate_error(valdtr, *arg,
				"the :type, :subtype, :contenttype, and :param "
				"arguments for the %s test are mutually exclusive, "
				"but more than one was specified",
				sieve_command_identifier(cmd));
			return FALSE;
		}
		if ( sieve_argument_is(tag, mime_type_tag) )
			data->mimeopt = EXT_MIME_OPTION_TYPE;
		else 	if ( sieve_argument_is(tag, mime_subtype_tag) )
			data->mimeopt = EXT_MIME_OPTION_SUBTYPE;
		else 	if ( sieve_argument_is(tag, mime_contenttype_tag) )
			data->mimeopt = EXT_MIME_OPTION_CONTENTTYPE;
		else 	if ( sieve_argument_is(tag, mime_param_tag) ) {
			/* Check syntax:
			 *   ":param" <param-list: string-list>
			 */
			if ( !sieve_validate_tag_parameter
				(valdtr, cmd, tag, *arg, NULL, 0, SAAT_STRING_LIST, FALSE) ) {
				return FALSE;
			}

			data->mimeopt = EXT_MIME_OPTION_PARAM;
			data->param_arg = *arg;

			/* Detach parameter */
			*arg = sieve_ast_arguments_detach(*arg,1);
		} else
			i_unreached();
	}
	return TRUE;
}

/*
 * Code generation
 */

static bool tag_mime_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
	struct sieve_command *cmd)
{
	struct tag_mime_data *data =
		(struct tag_mime_data *)arg->argument->data;

	if ( sieve_ast_argument_type(arg) != SAAT_TAG )
		return FALSE;

	sieve_opr_message_override_emit
		(cgenv->sblock, arg->argument->ext, &mime_header_override);

	(void)sieve_binary_emit_byte
		(cgenv->sblock, ( data->anychild ? 1 : 0 ));
	(void)sieve_binary_emit_byte
		(cgenv->sblock, data->mimeopt);
	if ( data->mimeopt == EXT_MIME_OPTION_PARAM &&
		!sieve_generate_argument(cgenv, data->param_arg, cmd) )
		return FALSE;
	return TRUE;
}

/*
 * Content-type stringlist
 */

enum content_type_part {
	CONTENT_TYPE_PART_NONE = 0,
	CONTENT_TYPE_PART_TYPE,
	CONTENT_TYPE_PART_SUBTYPE,
	CONTENT_TYPE_PART_CONTENTTYPE,
};

/* Object */

static int content_header_stringlist_next_item
	(struct sieve_stringlist *_strlist, string_t **str_r);
static void content_header_stringlist_reset
	(struct sieve_stringlist *_strlist);
static int content_header_stringlist_get_length
	(struct sieve_stringlist *_strlist);
static void content_header_stringlist_set_trace
	(struct sieve_stringlist *strlist, bool trace);

struct content_header_stringlist {
	struct sieve_stringlist strlist;

	struct sieve_header_list *source;

	enum ext_mime_option option;
	const char *const *params;

	const char *const *param_values;
};

static struct sieve_stringlist *content_header_stringlist_create
(const struct sieve_runtime_env *renv,
	struct sieve_header_list *source,
	enum ext_mime_option option, const char *const *params)
{
	struct content_header_stringlist *strlist;

	strlist = t_new(struct content_header_stringlist, 1);
	strlist->strlist.runenv = renv;
	strlist->strlist.exec_status = SIEVE_EXEC_OK;
	strlist->strlist.next_item = content_header_stringlist_next_item;
	strlist->strlist.reset = content_header_stringlist_reset;
	strlist->strlist.set_trace = content_header_stringlist_set_trace;
	strlist->source = source;
	strlist->option = option;
	strlist->params = params;

	if ( option != EXT_MIME_OPTION_PARAM ) {
		/* One header can have multiple parameters, so we cannot rely
		   on the source length for the :param option. */
		strlist->strlist.get_length = content_header_stringlist_get_length;
	}

	return &strlist->strlist;
}

/* Implementation */

static inline int _decode_hex_digit(const unsigned char digit)
{
	switch ( digit ) {
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		return digit - '0';

	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
		return digit - 'a' + 0x0a;

	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
		return digit - 'A' + 0x0A;
	}
	return -1;
}

static string_t *
content_type_param_decode(const char *value)
{
	const unsigned char *p, *plast;

	string_t *str = t_str_new(64);
	plast = p = (const unsigned char *)value;
	while ( *p != '\0' ) {
		unsigned char ch;
		int digit;

		if ( *p == '%' ) {
			if ( p - plast > 0 )
				str_append_data(str, plast, (p - plast));
			p++;
			if ( *p == '\0' || (digit=_decode_hex_digit(*p)) < 0 )
				return NULL;
			ch = (unsigned char)digit;
			p++;
			if ( *p == '\0' || (digit=_decode_hex_digit(*p)) < 0 )
				return NULL;
			ch = (ch << 4) + (unsigned char)digit;
			str_append_data(str, &ch, 1);
			plast = p + 1;
		}
		p++;
	}
	if ( p - plast > 0 )
		str_append_data(str, plast, (p - plast));
	return str;
}

static string_t *
content_type_param_next(struct content_header_stringlist *strlist)
{	
	const struct sieve_runtime_env *renv = strlist->strlist.runenv;
	const char *const *values = strlist->param_values;
	bool trace = strlist->strlist.trace;

	i_assert( strlist->params != NULL );

	/* Iterate over all parsed parameter values */
	for ( ; *values != NULL; values += 2 ) {
		const char *const *params = strlist->params;
		const char *name = values[0], *value = values[1];
		size_t nlen = strlen(name);

		/* Iterate over all interesting parameter names */
		for ( ; *params != NULL; params++ ) {
			size_t plen = strlen(*params);

			if ( plen != nlen &&
				(nlen != plen + 1 || name[nlen-1] != '*') )
				continue;

			if ( plen == nlen ) {
				if ( strcasecmp(name, *params) == 0 ) {
					/* Return raw value */
					if ( trace ) {
						sieve_runtime_trace(renv, 0,
							"found mime parameter `%s' in mime header",
							*params);
					}

					strlist->param_values = values + 2;
					return t_str_new_const(value, strlen(value));
				}
			} else {
				if ( trace ) {
					sieve_runtime_trace(renv, 0,
						"found encoded parameter `%s' in mime header",
						*params);
				}

				if ( strncasecmp(name, *params, plen) == 0 ) {
					string_t *result = NULL;

					strlist->param_values = values + 2;

					/* Decode value first */
					// FIXME: transcode charset
					value = strchr(value, '\'');
					if (value != NULL)
						value = strchr(value+1, '\'');
					if (value != NULL)
						result = content_type_param_decode(value + 1);
					if (result == NULL)
						strlist->param_values = NULL;
					return result;
				}
			}
		}
	}

	strlist->param_values = NULL;
	return NULL;
}

// FIXME: not too happy with the use of string_t like this.
// Sieve should have a special runtime string type (TODO)
static string_t *
content_header_parse(struct content_header_stringlist *strlist,
	const char *hdr_name, string_t *str)
{
	const struct sieve_runtime_env *renv = strlist->strlist.runenv;
	bool trace = strlist->strlist.trace;
	struct rfc822_parser_context parser;
	const char *type, *p;
	bool is_ctype = FALSE;
	string_t *content;

	if ( strlist->option == EXT_MIME_OPTION_NONE )
		return str;

	if ( strcasecmp(hdr_name, "content-type") == 0 )
		is_ctype = TRUE;
	else if ( strcasecmp(hdr_name, "content-disposition") != 0 ) {
		if ( trace ) {
			sieve_runtime_trace(renv, 0,
				"non-mime header yields empty string");
		}
		return t_str_new(0);
	}

	/* Initialize parsing */
	rfc822_parser_init(&parser, str_data(str), str_len(str), NULL);
	(void)rfc822_skip_lwsp(&parser);

	/* Parse content type/disposition */
	content = t_str_new(64);
	if ( is_ctype ){ 
		if (rfc822_parse_content_type(&parser, content) < 0) {
			str_truncate(content, 0);
			return content;
		}
	} else {
		if (rfc822_parse_mime_token(&parser, content) < 0) {
			str_truncate(content, 0);
			return content;
		}
	}

	/* Content-type value must end here, otherwise it is invalid after all */
	(void)rfc822_skip_lwsp(&parser);
	if ( parser.data != parser.end && *parser.data != ';' ) {
		str_truncate(content, 0);
		return content;
	}

	if ( strlist->option == EXT_MIME_OPTION_PARAM ) {
		string_t *param_val;

		/* MIME parameter */
		i_assert( strlist->params != NULL );

		// FIXME: not very nice when multiple parameters in the same header
		// are queried in successive tests.
		str_truncate(content, 0);
		rfc2231_parse(&parser, &strlist->param_values);

		param_val = content_type_param_next(strlist);
		if ( param_val != NULL )
			content = param_val;
	} else {
		/* Get :type/:subtype:/:contenttype value */
		type = str_c(content);
		p = strchr(type, '/');
		switch ( strlist->option ) {
		case EXT_MIME_OPTION_TYPE:
			if ( trace ) {
				sieve_runtime_trace(renv, 0,
					"extracted MIME type");
			}
			if ( p != NULL ) {
				i_assert( is_ctype );
				str_truncate(content, (p - type));
			}
			break;	
		case EXT_MIME_OPTION_SUBTYPE:
			if ( p == NULL ) {
				i_assert( !is_ctype );
				if ( trace ) {
					sieve_runtime_trace(renv, 0,
						"no MIME sub-type for content-disposition");
				}
				str_truncate(content, 0);
				break;
			}

			i_assert( is_ctype );
			if ( trace ) {
				sieve_runtime_trace(renv, 0,
					"extracted MIME sub-type");
			}
			str_delete(content, 0, (p - type) + 1);
			break;	
		case EXT_MIME_OPTION_CONTENTTYPE:
			sieve_runtime_trace(renv, 0,
				"extracted full MIME contenttype");
			break;
		default:
			break;
		}
	}

	/* Success */
	return content;
}

static int content_header_stringlist_next_item
(struct sieve_stringlist *_strlist, string_t **str_r)
{
	struct content_header_stringlist *strlist =
		(struct content_header_stringlist *)_strlist;
	const char *hdr_name;
	int ret;

	if ( strlist->param_values != NULL ) {
		string_t *param_val;

		i_assert( strlist->option == EXT_MIME_OPTION_PARAM );
		param_val = content_type_param_next(strlist);
		if ( param_val != NULL ) {
			*str_r = param_val;
			return 1;
		}
	}

	if ( (ret=sieve_header_list_next_item
		(strlist->source, &hdr_name, str_r)) <= 0 ) {
		if (ret < 0) {
			_strlist->exec_status =
				strlist->source->strlist.exec_status;
		}
		return ret;
	}

	*str_r = content_header_parse(strlist, hdr_name, *str_r);
	return 1;
}

static void content_header_stringlist_reset
(struct sieve_stringlist *_strlist)
{
	struct content_header_stringlist *strlist =
		(struct content_header_stringlist *)_strlist;
	sieve_header_list_reset(strlist->source);
}

static int content_header_stringlist_get_length
(struct sieve_stringlist *_strlist)
{
	struct content_header_stringlist *strlist =
		(struct content_header_stringlist *)_strlist;
	return sieve_header_list_get_length(strlist->source);
}

static void content_header_stringlist_set_trace
(struct sieve_stringlist *_strlist, bool trace)
{
	struct content_header_stringlist *strlist =
		(struct content_header_stringlist *)_strlist;
	sieve_header_list_set_trace(strlist->source, trace);
}

/*
 * Header override implementation
 */

/* Context data */

struct svmo_mime_context {
	enum ext_mime_option mimeopt;
	const char *const *params;
	unsigned int anychild:1;
};

/* Context coding */

static bool svmo_mime_dump_context
(const struct sieve_message_override *svmo ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	unsigned int anychild, mimeopt;

	if ( !sieve_binary_read_byte(denv->sblock, address, &anychild) )
		return FALSE;
	if ( anychild > 0 )
		sieve_code_dumpf(denv, "anychild");

	if ( !sieve_binary_read_byte(denv->sblock, address, &mimeopt) )
		return FALSE;

	switch ( mimeopt ) {
	case EXT_MIME_OPTION_NONE:
		break;
	case EXT_MIME_OPTION_TYPE:
		sieve_code_dumpf(denv, "option: type");
		break;
	case EXT_MIME_OPTION_SUBTYPE:
		sieve_code_dumpf(denv, "option: subtype");
		break;
	case EXT_MIME_OPTION_CONTENTTYPE:
		sieve_code_dumpf(denv, "option: contenttype");
		break;
	case EXT_MIME_OPTION_PARAM:
		sieve_code_dumpf(denv, "option: param");
		sieve_code_descend(denv);
		if ( !sieve_opr_stringlist_dump(denv, address, "param-list") )
			return FALSE;
		sieve_code_ascend(denv);
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

static int svmo_mime_read_context
(const struct sieve_message_override *svmo ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address,
	void **ho_context)
{
	pool_t pool = sieve_result_pool(renv->result); // FIXME: investigate
	struct svmo_mime_context *ctx;
	unsigned int anychild = 0, mimeopt = EXT_MIME_OPTION_NONE;
	struct sieve_stringlist *param_list = NULL;
	int ret;

	if ( !sieve_binary_read_byte
		(renv->sblock, address, &anychild) ) {
		sieve_runtime_trace_error(renv,
			"anychild: invalid byte");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	if ( !sieve_binary_read_byte
		(renv->sblock, address, &mimeopt) ||
		mimeopt > EXT_MIME_OPTION_PARAM ) {
		sieve_runtime_trace_error(renv,
			"option: invalid mime option code");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	if ( mimeopt == EXT_MIME_OPTION_PARAM &&
		(ret=sieve_opr_stringlist_read
			(renv, address, "param-list", &param_list)) <= 0 )
		return ret;

	ctx = p_new(pool, struct svmo_mime_context, 1);
	ctx->anychild = (anychild == 0 ? FALSE : TRUE);
	ctx->mimeopt = (enum ext_mime_option)mimeopt;

	if ( param_list != NULL && sieve_stringlist_read_all
		(param_list, pool, &ctx->params) < 0 ) {
		sieve_runtime_trace_error(renv,
			"failed to read param-list operand");
		return param_list->exec_status;
	}

	*ho_context = (void *) ctx;
	return SIEVE_EXEC_OK;
}

/* Override */

static int svmo_mime_header_override
(const struct sieve_message_override *svmo,
	const struct sieve_runtime_env *renv, bool mime_decode,
	struct sieve_stringlist **headers_r)
{
	struct svmo_mime_context *ctx =
		(struct svmo_mime_context *)svmo->context;
	struct ext_foreverypart_runtime_loop *sfploop;
	struct sieve_header_list *headers;
	struct sieve_stringlist *values;
	int ret;

	sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
		"header mime override:");
	sieve_runtime_trace_descend(renv);

	if ( ctx->anychild ) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
			"headers from current mime part and children");
	} else {
		sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
			"headers from current mime part");
	}

	sfploop = ext_foreverypart_runtime_loop_get_current(renv);
	if ( sfploop != NULL ) {
		headers = sieve_mime_header_list_create
			(renv, *headers_r, &sfploop->part_iter,
				mime_decode, ctx->anychild);
	} else if ( ctx->anychild ) {
		struct sieve_message_part_iter part_iter;

		if ( (ret=sieve_message_part_iter_init
			(&part_iter, renv)) <= 0 )
			return ret;

		headers = sieve_mime_header_list_create
			(renv, *headers_r, &part_iter, mime_decode, TRUE);
	} else {
		headers = sieve_message_header_list_create
			(renv, *headers_r, mime_decode);
	}
	values = &headers->strlist;

	switch ( ctx->mimeopt ) {
	case EXT_MIME_OPTION_NONE:
		break;
	case EXT_MIME_OPTION_TYPE:
		sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
			"extract mime type from header value");
		break;
	case EXT_MIME_OPTION_SUBTYPE:
		sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
			"extract mime subtype from header value");
		break;
	case EXT_MIME_OPTION_CONTENTTYPE:
		sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
			"extract mime contenttype from header value");
		break;
	case EXT_MIME_OPTION_PARAM:
		sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
			"extract mime parameters from header value");
		break;
	default:
		i_unreached();
	}

	if ( ctx->mimeopt != EXT_MIME_OPTION_NONE ) {
		values = content_header_stringlist_create
			(renv, headers, ctx->mimeopt, ctx->params);
	}
	*headers_r = values;

	sieve_runtime_trace_ascend(renv);
	return SIEVE_EXEC_OK;
}

