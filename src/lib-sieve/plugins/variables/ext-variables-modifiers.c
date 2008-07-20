#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-binary.h"

#include "ext-variables-common.h"
#include "ext-variables-modifiers.h"

#include <ctype.h>

/*
 * Core modifiers
 */
 
extern const struct sieve_variables_modifier lower_modifier;
extern const struct sieve_variables_modifier upper_modifier;
extern const struct sieve_variables_modifier lowerfirst_modifier;
extern const struct sieve_variables_modifier upperfirst_modifier;
extern const struct sieve_variables_modifier quotewildcard_modifier;
extern const struct sieve_variables_modifier length_modifier;

const struct sieve_variables_modifier *ext_variables_core_modifiers[] = {
	&lower_modifier,
	&upper_modifier,
	&lowerfirst_modifier,
	&upperfirst_modifier,
	&quotewildcard_modifier,
	&length_modifier
};

const unsigned int ext_variables_core_modifiers_count =
    N_ELEMENTS(ext_variables_core_modifiers);

/*
 * Set modifier registry
 */

void sieve_variables_modifier_register
(struct sieve_validator *valdtr, const struct sieve_variables_modifier *smodf, 
	int ext_id) 
{
	struct ext_variables_validator_context *ctx = 
		ext_variables_validator_context_get(valdtr);
	
	sieve_validator_object_registry_add
		(ctx->modifiers, &smodf->object, ext_id);
}

const struct sieve_variables_modifier *ext_variables_modifier_find
(struct sieve_validator *valdtr, const char *identifier, int *ext_id_r)
{
	struct ext_variables_validator_context *ctx = 
		ext_variables_validator_context_get(valdtr);
		
	const struct sieve_object *object = 
		sieve_validator_object_registry_find
			(ctx->modifiers, identifier, ext_id_r);

	return (const struct sieve_variables_modifier *) object;
}

void ext_variables_register_core_modifiers
(struct ext_variables_validator_context *ctx)
{
	unsigned int i;
	
	/* Register core testsuite objects */
	for ( i = 0; i < ext_variables_core_modifiers_count; i++ ) {
		sieve_validator_object_registry_add
			(ctx->modifiers, &(ext_variables_core_modifiers[i]->object), 
				ext_variables_my_id);
	}
}

/*
 * Set modifier coding
 */
 
const struct sieve_operand_class ext_variables_modifier_operand_class = 
	{ "MODIFIER" };
	
static const struct sieve_extension_obj_registry core_modifiers =
	SIEVE_VARIABLES_DEFINE_MODIFIERS(ext_variables_core_modifiers);

const struct sieve_operand modifier_operand = { 
	"modifier", 
	&variables_extension,
	EXT_VARIABLES_OPERAND_MODIFIER, 
	&ext_variables_modifier_operand_class,
	&core_modifiers
};

/* 
 * Core modifiers 
 */
 
/* Forward declarations */

bool mod_lower_modify(string_t *in, string_t **result);
bool mod_upper_modify(string_t *in, string_t **result);
bool mod_lowerfirst_modify(string_t *in, string_t **result);
bool mod_upperfirst_modify(string_t *in, string_t **result);
bool mod_length_modify(string_t *in, string_t **result);
bool mod_quotewildcard_modify(string_t *in, string_t **result);

/* Modifier objects */

const struct sieve_variables_modifier lower_modifier = {
	SIEVE_OBJECT("lower", &modifier_operand, EXT_VARIABLES_MODIFIER_LOWER),
	40,
	mod_lower_modify
};

const struct sieve_variables_modifier upper_modifier = {
	SIEVE_OBJECT("upper", &modifier_operand, EXT_VARIABLES_MODIFIER_UPPER),
	40,
	mod_upper_modify
};

const struct sieve_variables_modifier lowerfirst_modifier = {
	SIEVE_OBJECT
		("lowerfirst", &modifier_operand, EXT_VARIABLES_MODIFIER_LOWERFIRST),
	30,
	mod_lowerfirst_modify
};

const struct sieve_variables_modifier upperfirst_modifier = {
	SIEVE_OBJECT
		("upperfirst", &modifier_operand,	EXT_VARIABLES_MODIFIER_UPPERFIRST),
	30,
	mod_upperfirst_modify
};

const struct sieve_variables_modifier quotewildcard_modifier = {
	SIEVE_OBJECT
		("quotewildcard", &modifier_operand, EXT_VARIABLES_MODIFIER_QUOTEWILDCARD),
	20,
	mod_quotewildcard_modify
};

const struct sieve_variables_modifier length_modifier = {
	SIEVE_OBJECT("length", &modifier_operand, EXT_VARIABLES_MODIFIER_LENGTH),
	10,
	mod_length_modify
};

/* Modifier implementations */

bool mod_upperfirst_modify(string_t *in, string_t **result)
{
	char *content;
	
	*result = t_str_new(str_len(in));
	str_append_str(*result, in);
		
	content = str_c_modifiable(*result);
	content[0] = toupper(content[0]);

	return TRUE;
}

bool mod_lowerfirst_modify(string_t *in, string_t **result)
{
	char *content;
	
	*result = t_str_new(str_len(in));
	str_append_str(*result, in);
		
	content = str_c_modifiable(*result);
	content[0] = i_tolower(content[0]);

	return TRUE;
}

bool mod_upper_modify(string_t *in, string_t **result)
{
	char *content;
	
	*result = t_str_new(str_len(in));
	str_append_str(*result, in);

	content = str_c_modifiable(*result);
	content = str_ucase(content);
	
	return TRUE;
}

bool mod_lower_modify(string_t *in, string_t **result)
{
	char *content;
	
	*result = t_str_new(str_len(in));
	str_append_str(*result, in);

	content = str_c_modifiable(*result);
	content = str_lcase(content);

	return TRUE;
}

bool mod_length_modify(string_t *in, string_t **result)
{
	*result = t_str_new(64);
	str_printfa(*result, "%d", str_len(in));

	return TRUE;
}

bool mod_quotewildcard_modify(string_t *in, string_t **result)
{
	unsigned int i;
	const char *content;
	
	*result = t_str_new(str_len(in) * 2);
	content = (const char *) str_data(in);
	
	for ( i = 0; i < str_len(in); i++ ) {
		if ( content[i] == '*' || content[i] == '?' || content[i] == '\\' ) {
			str_append_c(*result, '\\');
		}
		str_append_c(*result, content[i]);
	}
	
	return TRUE;
}








