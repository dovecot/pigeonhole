/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "unichar.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-runtime.h"

#include "ext-variables-common.h"
#include "ext-variables-limits.h"
#include "ext-variables-modifiers.h"

#include <ctype.h>

/*
 * Core modifiers
 */

extern const struct sieve_variables_modifier_def lower_modifier;
extern const struct sieve_variables_modifier_def upper_modifier;
extern const struct sieve_variables_modifier_def lowerfirst_modifier;
extern const struct sieve_variables_modifier_def upperfirst_modifier;
extern const struct sieve_variables_modifier_def quotewildcard_modifier;
extern const struct sieve_variables_modifier_def length_modifier;

enum ext_variables_modifier_code {
    EXT_VARIABLES_MODIFIER_LOWER,
    EXT_VARIABLES_MODIFIER_UPPER,
    EXT_VARIABLES_MODIFIER_LOWERFIRST,
    EXT_VARIABLES_MODIFIER_UPPERFIRST,
    EXT_VARIABLES_MODIFIER_QUOTEWILDCARD,
    EXT_VARIABLES_MODIFIER_LENGTH,
    EXT_VARIABLES_MODIFIER_COUNT
};

const struct sieve_variables_modifier_def *ext_variables_core_modifiers[] = {
	&lower_modifier,
	&upper_modifier,
	&lowerfirst_modifier,
	&upperfirst_modifier,
	&quotewildcard_modifier,
	&length_modifier,
};
static_assert_array_size(ext_variables_core_modifiers,
			 EXT_VARIABLES_MODIFIER_COUNT);

const unsigned int ext_variables_core_modifiers_count =
    N_ELEMENTS(ext_variables_core_modifiers);

#define ext_variables_modifier_name(modf) \
	(modf)->object->def->name
#define ext_variables_modifiers_equal(modf1, modf2) \
	((modf1)->def == (modf2)->def)
#define ext_variables_modifiers_equal_precedence(modf1, modf2) \
	((modf1)->def->precedence == (modf2)->def->precendence)

/*
 * Modifier registry
 */

void sieve_variables_modifier_register(
	const struct sieve_extension *var_ext, struct sieve_validator *valdtr,
	const struct sieve_extension *ext,
	const struct sieve_variables_modifier_def *smodf_def)
{
	struct ext_variables_validator_context *ctx =
		ext_variables_validator_context_get(var_ext, valdtr);

	sieve_validator_object_registry_add(ctx->modifiers, ext,
					    &smodf_def->obj_def);
}

bool ext_variables_modifier_exists(const struct sieve_extension *var_ext,
				   struct sieve_validator *valdtr,
				   const char *identifier)
{
	struct ext_variables_validator_context *ctx =
		ext_variables_validator_context_get(var_ext, valdtr);

	return sieve_validator_object_registry_find(ctx->modifiers,
						    identifier, NULL);
}

const struct sieve_variables_modifier *
ext_variables_modifier_create_instance(const struct sieve_extension *var_ext,
				       struct sieve_validator *valdtr,
				       struct sieve_command *cmd,
				       const char *identifier)
{
	struct ext_variables_validator_context *ctx =
		ext_variables_validator_context_get(var_ext, valdtr);
	struct sieve_object object;
	struct sieve_variables_modifier *modf;
	pool_t pool;

	if (!sieve_validator_object_registry_find(ctx->modifiers, identifier,
						  &object))
		return NULL;

	pool = sieve_command_pool(cmd);
	modf = p_new(pool, struct sieve_variables_modifier, 1);
	modf->object = object;
	modf->var_ext = var_ext;
	modf->def = (const struct sieve_variables_modifier_def *) object.def;

	return modf;
}

void ext_variables_register_core_modifiers(
	const struct sieve_extension *ext,
	struct ext_variables_validator_context *ctx)
{
	unsigned int i;

	/* Register core modifiers*/
	for (i = 0; i < ext_variables_core_modifiers_count; i++) {
		sieve_validator_object_registry_add(
			ctx->modifiers, ext,
			&(ext_variables_core_modifiers[i]->obj_def));
	}
}

/*
 * Core modifiers
 */

/* Forward declarations */

static bool
mod_lower_modify(const struct sieve_variables_modifier *modf,
		 string_t *in, string_t **result);
static bool
mod_upper_modify(const struct sieve_variables_modifier *modf,
		 string_t *in, string_t **result);
static bool
mod_lowerfirst_modify(const struct sieve_variables_modifier *modf,
		      string_t *in, string_t **result);
static bool
mod_upperfirst_modify(const struct sieve_variables_modifier *modf,
		      string_t *in, string_t **result);
static bool
mod_length_modify(const struct sieve_variables_modifier *modf,
		  string_t *in, string_t **result);
static bool
mod_quotewildcard_modify(const struct sieve_variables_modifier *modf,
			 string_t *in, string_t **result);

/* Modifier objects */

const struct sieve_variables_modifier_def lower_modifier = {
	SIEVE_OBJECT("lower", &modifier_operand, EXT_VARIABLES_MODIFIER_LOWER),
	40,
	mod_lower_modify,
};

const struct sieve_variables_modifier_def upper_modifier = {
	SIEVE_OBJECT("upper", &modifier_operand, EXT_VARIABLES_MODIFIER_UPPER),
	40,
	mod_upper_modify,
};

const struct sieve_variables_modifier_def lowerfirst_modifier = {
	SIEVE_OBJECT("lowerfirst", &modifier_operand,
		     EXT_VARIABLES_MODIFIER_LOWERFIRST),
	30,
	mod_lowerfirst_modify,
};

const struct sieve_variables_modifier_def upperfirst_modifier = {
	SIEVE_OBJECT("upperfirst", &modifier_operand,
		     EXT_VARIABLES_MODIFIER_UPPERFIRST),
	30,
	mod_upperfirst_modify,
};

const struct sieve_variables_modifier_def quotewildcard_modifier = {
	SIEVE_OBJECT("quotewildcard", &modifier_operand,
		     EXT_VARIABLES_MODIFIER_QUOTEWILDCARD),
	20,
	mod_quotewildcard_modify,
};

const struct sieve_variables_modifier_def length_modifier = {
	SIEVE_OBJECT("length", &modifier_operand,
		     EXT_VARIABLES_MODIFIER_LENGTH),
	10,
	mod_length_modify,
};

/* Modifier implementations */

static bool
mod_upperfirst_modify(const struct sieve_variables_modifier *modf ATTR_UNUSED,
		      string_t *in, string_t **result)
{
	char *content;

	if (str_len(in) == 0) {
		*result = in;
		return TRUE;
	}

	*result = t_str_new(str_len(in));
	str_append_str(*result, in);

	content = str_c_modifiable(*result);
	content[0] = i_toupper(content[0]);
	return TRUE;
}

static bool
mod_lowerfirst_modify(const struct sieve_variables_modifier *modf ATTR_UNUSED,
		      string_t *in, string_t **result)
{
	char *content;

	if (str_len(in) == 0) {
		*result = in;
		return TRUE;
	}

	*result = t_str_new(str_len(in));
	str_append_str(*result, in);

	content = str_c_modifiable(*result);
	content[0] = i_tolower(content[0]);
	return TRUE;
}

static bool
mod_upper_modify(const struct sieve_variables_modifier *modf ATTR_UNUSED,
		 string_t *in, string_t **result)
{
	char *content;

	if (str_len(in) == 0) {
		*result = in;
		return TRUE;
	}

	*result = t_str_new(str_len(in));
	str_append_str(*result, in);

	content = str_c_modifiable(*result);
	(void)str_ucase(content);
	return TRUE;
}

static bool
mod_lower_modify(const struct sieve_variables_modifier *modf ATTR_UNUSED,
		 string_t *in, string_t **result)
{
	char *content;

	if (str_len(in) == 0) {
		*result = in;
		return TRUE;
	}

	*result = t_str_new(str_len(in));
	str_append_str(*result, in);

	content = str_c_modifiable(*result);
	(void)str_lcase(content);
	return TRUE;
}

static bool
mod_length_modify(const struct sieve_variables_modifier *modf ATTR_UNUSED,
		  string_t *in, string_t **result)
{
	*result = t_str_new(64);
	str_printfa(*result, "%llu", (unsigned long long)
		    uni_utf8_strlen_n(str_data(in), str_len(in)));
	return TRUE;
}

static bool
mod_quotewildcard_modify(const struct sieve_variables_modifier *modf,
			 string_t *in, string_t **result)
{
	size_t max_var_size =
		sieve_variables_get_max_variable_size(modf->var_ext);
	const unsigned char *p, *poff, *pend;
	size_t new_size;

	if (str_len(in) == 0) {
		/* Empty string */
		*result = in;
		return TRUE;
	}

	/* Allocate new string */
	new_size = str_len(in) + 16;
	if (new_size > max_var_size)
		new_size = max_var_size;
	*result = t_str_new(new_size + 1);

	/* Escape string */
	p = str_data(in);
	pend = p + str_len(in);
	poff = p;
	while (p < pend) {
		unsigned int n = uni_utf8_char_bytes((char)*p);

		if (n == 1 && (*p == '*' || *p == '?' || *p == '\\')) {
			str_append_data(*result, poff, p - poff);
			poff = p;

			if (str_len(*result) + 2 > max_var_size)
				break;

			str_append_c(*result, '\\');
		} else if ((str_len(*result) + (p - poff) + n) > max_var_size) {
			break;
		}
		if (p + n > pend) {
			p = pend;
			break;
		}
		p += n;
	}

	str_append_data(*result, poff, p - poff);

	return TRUE;
}

/*
 * Modifier argument
 */

/* [MODIFIER]:
 *   ":lower" / ":upper" / ":lowerfirst" / ":upperfirst" /
 *             ":quotewildcard" / ":length"
 */

/* Forward declarations */

static bool
tag_modifier_is_instance_of(struct sieve_validator *valdtr,
			    struct sieve_command *cmd,
			    const struct sieve_extension *ext,
			    const char *identifier, void **context);

/* Modifier tag object */

static const struct sieve_argument_def modifier_tag = {
	.identifier = "MODIFIER",
	.flags = SIEVE_ARGUMENT_FLAG_MULTIPLE,
	.is_instance_of = tag_modifier_is_instance_of,
};

/* Modifier tag implementation */

static bool
tag_modifier_is_instance_of(struct sieve_validator *valdtr,
			    struct sieve_command *cmd,
			    const struct sieve_extension *ext,
			    const char *identifier, void **data)
{
	const struct sieve_variables_modifier *modf;

	if (data == NULL)
		return ext_variables_modifier_exists(ext, valdtr, identifier);

	modf = ext_variables_modifier_create_instance(ext, valdtr, cmd,
						      identifier);
	if (modf == NULL)
		return FALSE;

	*data = (void *)modf;
	return TRUE;
}

/* Registration */

void sieve_variables_modifiers_link_tag(
	struct sieve_validator *valdtr,	const struct sieve_extension *var_ext,
	struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag(valdtr, cmd_reg, var_ext,
				     &modifier_tag, 0);
}

/* Validation */

bool sieve_variables_modifiers_validate(
	struct sieve_validator *valdtr, struct sieve_command *cmd,
	ARRAY_TYPE(sieve_variables_modifier) *modifiers)
{
	struct sieve_ast_argument *arg;

	arg = sieve_command_first_argument(cmd);
	while (arg != NULL && arg != cmd->first_positional) {
		const struct sieve_variables_modifier *modfs;
		const struct sieve_variables_modifier *modf;
		unsigned int i, modf_count;
		bool inserted;

		if (!sieve_argument_is(arg, modifier_tag)) {
			arg = sieve_ast_argument_next(arg);
			continue;
		}
		modf = (const struct sieve_variables_modifier *)
			arg->argument->data;

		inserted = FALSE;
		modfs = array_get(modifiers, &modf_count);
		for (i = 0; i < modf_count && !inserted; i++) {

			if (modfs[i].def->precedence == modf->def->precedence) {
				sieve_argument_validate_error(
					valdtr, arg,
					"modifiers :%s and :%s specified for the set command conflict "
					"having equal precedence",
					modfs[i].def->obj_def.identifier,
					modf->def->obj_def.identifier);
				return FALSE;
			}
			if (modfs[i].def->precedence < modf->def->precedence) {
				array_insert(modifiers, i, modf, 1);
				inserted = TRUE;
			}
		}

		if (!inserted)
			array_append(modifiers, modf, 1);

		/* Added to modifier list; self-destruct to prevent implicit
		   code generation.
		 */
		arg = sieve_ast_arguments_detach(arg, 1);
	}
	return TRUE;
}

bool sieve_variables_modifiers_generate(
	const struct sieve_codegen_env *cgenv,
	ARRAY_TYPE(sieve_variables_modifier) *modifiers)
{
	struct sieve_binary_block *sblock = cgenv->sblock;
	const struct sieve_variables_modifier *modfs;
	unsigned int i, modf_count;

	sieve_binary_emit_byte(sblock, array_count(modifiers));

	modfs = array_get(modifiers, &modf_count);
	for (i = 0; i < modf_count; i++) {
		ext_variables_opr_modifier_emit(
			sblock, modfs[i].object.ext, modfs[i].def);
	}
	return TRUE;
}

/*
 * Modifier coding
 */

const struct sieve_operand_class sieve_variables_modifier_operand_class =
	{ "modifier" };

static const struct sieve_extension_objects core_modifiers =
	SIEVE_VARIABLES_DEFINE_MODIFIERS(ext_variables_core_modifiers);

const struct sieve_operand_def modifier_operand = {
	.name = "modifier",
	.ext_def = &variables_extension,
	.code = EXT_VARIABLES_OPERAND_MODIFIER,
	.class = &sieve_variables_modifier_operand_class,
	.interface = &core_modifiers,
};

bool sieve_variables_modifiers_code_dump(const struct sieve_dumptime_env *denv,
					 sieve_size_t *address)
{
	unsigned int mdfs, i;

	/* Read the number of applied modifiers we need to read */
	if (!sieve_binary_read_byte(denv->sblock, address, &mdfs))
		return FALSE;

	/* Print all modifiers (sorted during code generation already) */
	for (i = 0; i < mdfs; i++) {
		if (!ext_variables_opr_modifier_dump(denv, address))
			return FALSE;
	}
	return TRUE;
}

int sieve_variables_modifiers_code_read(
	const struct sieve_runtime_env *renv,
	const struct sieve_extension *var_ext,
	sieve_size_t *address, ARRAY_TYPE(sieve_variables_modifier) *modifiers)
{
	unsigned int lprec, mdfs, i;

	if (!sieve_binary_read_byte(renv->sblock, address, &mdfs)) {
		sieve_runtime_trace_error(renv, "invalid modifier count");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	t_array_init(modifiers, mdfs);

	lprec = (unsigned int)-1;
	for (i = 0; i < mdfs; i++) {
		struct sieve_variables_modifier modf;

		if (!ext_variables_opr_modifier_read(renv, var_ext,
						     address, &modf))
			return SIEVE_EXEC_BIN_CORRUPT;
		if (modf.def != NULL) {
			if (modf.def->precedence >= lprec) {
				sieve_runtime_trace_error(
					renv, "unsorted modifier precedence");
				return SIEVE_EXEC_BIN_CORRUPT;
			}
			lprec = modf.def->precedence;
		}
		array_append(modifiers, &modf, 1);
	}

	return SIEVE_EXEC_OK;
}

/*
 * Modifier application
 */

int sieve_variables_modifiers_apply(
	const struct sieve_runtime_env *renv,
	const struct sieve_extension *var_ext,
	ARRAY_TYPE(sieve_variables_modifier) *modifiers, string_t **value)
{
	const struct ext_variables_context *extctx =
		ext_variables_get_context(var_ext);
	const struct sieve_variables_modifier *modfs;
	unsigned int i, modf_count;

	/* Hold value within limits */
	if (str_len(*value) > extctx->set->max_variable_size) {
		/* assume variable originates from code, so copy it first */
		string_t *new_value =
			t_str_new(extctx->set->max_variable_size+3);
		str_append_str(new_value, *value);
		*value = new_value;
		str_truncate_utf8(*value, extctx->set->max_variable_size);
	}

	if (!array_is_created(modifiers))
		return SIEVE_EXEC_OK;

	modfs = array_get(modifiers, &modf_count);
	if (modf_count == 0)
		return SIEVE_EXEC_OK;

	for (i = 0; i < modf_count; i++) {
		string_t *new_value;
		const struct sieve_variables_modifier *modf = &modfs[i];

		if (modf->def != NULL && modf->def->modify != NULL) {
			if (!modf->def->modify(modf, *value, &new_value))
				return SIEVE_EXEC_FAILURE;

			*value = new_value;
			if (*value == NULL)
				return SIEVE_EXEC_FAILURE;

			sieve_runtime_trace_here(
				renv, SIEVE_TRLVL_COMMANDS,
				"modify :%s \"%s\" => \"%s\"",
				sieve_variables_modifier_name(modf),
				str_sanitize(str_c(*value), 256),
				str_sanitize(str_c(new_value), 256));

			/* Hold value within limits */
			if (str_len(*value) > extctx->set->max_variable_size) {
				str_truncate_utf8(
					*value, extctx->set->max_variable_size);
			}
		}
	}
	return SIEVE_EXEC_OK;
}
