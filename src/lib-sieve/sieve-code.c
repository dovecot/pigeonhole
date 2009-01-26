/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */
 
#include "lib.h"
#include "str.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-extensions.h"
#include "sieve-actions.h"
#include "sieve-binary.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "sieve-code.h"

#include <stdio.h>

/* 
 * Coded stringlist
 */

struct sieve_coded_stringlist {
	const struct sieve_runtime_env *runenv;
	sieve_size_t start_address;
	sieve_size_t end_address;
	sieve_size_t current_offset;
	unsigned int length;
	unsigned int index;
};

static struct sieve_coded_stringlist *sieve_coded_stringlist_create
(const struct sieve_runtime_env *renv, 
	 sieve_size_t start_address, unsigned int length, sieve_size_t end)
{
	struct sieve_coded_stringlist *strlist;
	
	if ( end > sieve_binary_get_code_size(renv->sbin) ) 
  		return NULL;
    
	strlist = t_new(struct sieve_coded_stringlist, 1);
	strlist->runenv = renv;
	strlist->start_address = start_address;
	strlist->current_offset = start_address;
	strlist->end_address = end;
	strlist->length = length;
	strlist->index = 0;
  
	return strlist;
}

bool sieve_coded_stringlist_next_item
(struct sieve_coded_stringlist *strlist, string_t **str_r) 
{
	sieve_size_t address;
	*str_r = NULL;
  
	if ( strlist->index >= strlist->length ) 
		return TRUE;
	else {
		address = strlist->current_offset;
  	
		if ( sieve_opr_string_read(strlist->runenv, &address, str_r) ) {
			strlist->index++;
			strlist->current_offset = address;
			return TRUE;
		}
	}  
  
	return FALSE;
}

void sieve_coded_stringlist_reset(struct sieve_coded_stringlist *strlist) 
{  
	strlist->current_offset = strlist->start_address;
	strlist->index = 0;
}

unsigned int sieve_coded_stringlist_get_length
(struct sieve_coded_stringlist *strlist)
{
	return strlist->length;
}

sieve_size_t sieve_coded_stringlist_get_end_address
(struct sieve_coded_stringlist *strlist)
{
	return strlist->end_address;
}

sieve_size_t sieve_coded_stringlist_get_current_offset
(struct sieve_coded_stringlist *strlist)
{
	return strlist->current_offset;
}

bool sieve_coded_stringlist_read_all
(struct sieve_coded_stringlist *strlist, pool_t pool,
	const char * const **list_r)
{
	bool result = FALSE;
	ARRAY_DEFINE(items, const char *);
	string_t *item;
	
	sieve_coded_stringlist_reset(strlist);
	
	p_array_init(&items, pool, 4);
	
	item = NULL;
	while ( (result=sieve_coded_stringlist_next_item(strlist, &item)) && 
		item != NULL ) {
		const char *stritem = p_strdup(pool, str_c(item));
		
		array_append(&items, &stritem, 1);
	}
	
	(void)array_append_space(&items);
	*list_r = array_idx(&items, 0);

	return result;
}

static bool sieve_coded_stringlist_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address, 
	unsigned int length, sieve_size_t end, const char *field_name)
{
	unsigned int i;
	
	if ( end > sieve_binary_get_code_size(denv->sbin) ) 
  		return FALSE;
    
	if ( field_name != NULL )
		sieve_code_dumpf(denv, "%s: STRLIST [%u] (end: %08llx)", 
			field_name, length, (unsigned long long) end);
	else
		sieve_code_dumpf(denv, "STRLIST [%u] (end: %08llx)", 
			length, (unsigned long long) end);
	
	sieve_code_descend(denv);
	
	for ( i = 0; i < length; i++ ) {
		bool success = TRUE;

		T_BEGIN { 		
			success = sieve_opr_string_dump(denv, address, NULL);
		} T_END;

		if ( !success || *address > end ) 
			return FALSE;
	}

	if ( *address != end ) return FALSE;
	
	sieve_code_ascend(denv);
		
	return TRUE;
}
	
/*
 * Source line coding
 */

void sieve_code_source_line_emit
(struct sieve_binary *sbin, unsigned int source_line)
{
    (void)sieve_binary_emit_unsigned(sbin, source_line);
}

bool sieve_code_source_line_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
    unsigned int number = 0;

	sieve_code_mark(denv);
    if (sieve_binary_read_unsigned(denv->sbin, address, &number) ) {
        sieve_code_dumpf(denv, "(source line: %lu)", (unsigned long) number);

        return TRUE;
    }

    return FALSE;
}

bool sieve_code_source_line_read
(const struct sieve_runtime_env *renv, sieve_size_t *address,
	unsigned int *source_line_r)
{
	return sieve_binary_read_unsigned(renv->sbin, address, source_line_r);
}

/*
 * Core operands
 */
 
extern const struct sieve_operand comparator_operand;
extern const struct sieve_operand match_type_operand;
extern const struct sieve_operand address_part_operand;

const struct sieve_operand *sieve_operands[] = {
	&omitted_operand, /* SIEVE_OPERAND_OPTIONAL */
	&number_operand,
	&string_operand,
	&stringlist_operand,
	&comparator_operand,
	&match_type_operand,
	&address_part_operand,
	&catenated_string_operand
}; 

const unsigned int sieve_operand_count =
	N_ELEMENTS(sieve_operands);

/* 
 * Operand functions 
 */

sieve_size_t sieve_operand_emit_code
(struct sieve_binary *sbin, const struct sieve_operand *opr)
{
	sieve_size_t address;

	if ( opr->extension != NULL ) {
		address = sieve_binary_emit_extension
			(sbin, opr->extension, sieve_operand_count);
	
		sieve_binary_emit_extension_object
			(sbin, &opr->extension->operands, opr->code);

		return address;
	}

	return  sieve_binary_emit_byte(sbin, opr->code);
}

const struct sieve_operand *sieve_operand_read
(struct sieve_binary *sbin, sieve_size_t *address) 
{
	const struct sieve_extension *ext;
	unsigned int code = sieve_operand_count;

	if ( !sieve_binary_read_extension(sbin, address, &code, &ext) )
		return NULL;

	if ( !ext )
		return code < sieve_operand_count ? sieve_operands[code] : NULL;

	return (const struct sieve_operand *) sieve_binary_read_extension_object
		(sbin, address, &ext->operands);
}

bool sieve_operand_optional_present
(struct sieve_binary *sbin, sieve_size_t *address)
{	
	sieve_size_t tmp_addr = *address;
	unsigned int op = -1;
	
	if ( sieve_binary_read_byte(sbin, &tmp_addr, &op) && 
		(op == SIEVE_OPERAND_OPTIONAL) ) {
		*address = tmp_addr;
		return TRUE;
	}
	
	return FALSE;
}

bool sieve_operand_optional_read
(struct sieve_binary *sbin, sieve_size_t *address, int *id_code)
{
	if ( sieve_binary_read_code(sbin, address, id_code) ) 
		return TRUE;
	
	*id_code = 0;

	return FALSE;
}

/* 
 * Operand definitions
 */

/* Omitted */

const struct sieve_operand_class omitted_class =
	{ "OMITTED" };

const struct sieve_operand omitted_operand = {
	"@OMITTED",
	NULL, SIEVE_OPERAND_OPTIONAL,	
	&omitted_class, NULL
};
 
/* Number */

static bool opr_number_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address,
		const char *field_name);
static bool opr_number_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, 
		sieve_number_t *number_r);

const struct sieve_opr_number_interface number_interface = { 
	opr_number_dump, 
	opr_number_read
};

const struct sieve_operand_class number_class = 
	{ "number" };
	
const struct sieve_operand number_operand = { 
	"@number", 
	NULL, SIEVE_OPERAND_NUMBER,
	&number_class,
	&number_interface 
};

/* String */

static bool opr_string_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address,
		const char *field_name);
static bool opr_string_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str_r);

const struct sieve_opr_string_interface string_interface ={ 
	opr_string_dump,
	opr_string_read
};
	
const struct sieve_operand_class string_class = 
	{ "string" };
	
const struct sieve_operand string_operand = { 
	"@string", 
	NULL, SIEVE_OPERAND_STRING,
	&string_class,
	&string_interface
};	

/* String List */

static bool opr_stringlist_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address,
		const char *field_name);
static struct sieve_coded_stringlist *opr_stringlist_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_opr_stringlist_interface stringlist_interface = { 
	opr_stringlist_dump, 
	opr_stringlist_read
};

const struct sieve_operand_class stringlist_class = 
	{ "string-list" };

const struct sieve_operand stringlist_operand =	{ 
	"@string-list", 
	NULL, SIEVE_OPERAND_STRING_LIST,
	&stringlist_class, 
	&stringlist_interface
};

/* Catenated String */

static bool opr_catenated_string_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str);
static bool opr_catenated_string_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address,
		const char *field_name);

const struct sieve_opr_string_interface catenated_string_interface = { 
	opr_catenated_string_dump,
	opr_catenated_string_read
};
		
const struct sieve_operand catenated_string_operand = { 
	"@catenated-string", 
	NULL, SIEVE_OPERAND_CATENATED_STRING,
	&string_class,
	&catenated_string_interface
};	
	
/* 
 * Operand implementations 
 */

/* Omitted */

void sieve_opr_omitted_emit(struct sieve_binary *sbin)
{
    (void) sieve_operand_emit_code(sbin, &omitted_operand);
}
 
/* Number */

void sieve_opr_number_emit(struct sieve_binary *sbin, sieve_number_t number) 
{
	(void) sieve_operand_emit_code(sbin, &number_operand);
	(void) sieve_binary_emit_integer(sbin, number);
}

bool sieve_opr_number_dump_data
(const struct sieve_dumptime_env *denv, const struct sieve_operand *operand,
	sieve_size_t *address, const char *field_name) 
{
	const struct sieve_opr_number_interface *intf;

	if ( !sieve_operand_is_number(operand) ) 
		return FALSE;
		
	intf = (const struct sieve_opr_number_interface *) operand->interface; 
	
	if ( intf->dump == NULL )
		return FALSE;

	return intf->dump(denv, address, field_name);  
}

bool sieve_opr_number_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	const struct sieve_operand *operand;
	
	sieve_code_mark(denv);
	
	operand = sieve_operand_read(denv->sbin, address);

	return sieve_opr_number_dump_data(denv, operand, address, field_name);
}

bool sieve_opr_number_read_data
(const struct sieve_runtime_env *renv, const struct sieve_operand *operand,
	sieve_size_t *address, sieve_number_t *number_r)
{
	const struct sieve_opr_number_interface *intf;
		
	if ( !sieve_operand_is_number(operand) ) 
		return FALSE;	
		
	intf = (const struct sieve_opr_number_interface *) operand->interface; 
	
	if ( intf->read == NULL )
		return FALSE;

	return intf->read(renv, address, number_r);  
}

bool sieve_opr_number_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, 
	sieve_number_t *number_r)
{
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);
		
	return sieve_opr_number_read_data(renv, operand, address, number_r);
}

static bool opr_number_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	sieve_number_t number = 0;
	
	if (sieve_binary_read_integer(denv->sbin, address, &number) ) {
		if ( field_name != NULL ) 
			sieve_code_dumpf(denv, "%s: NUM %llu", field_name, (unsigned long long) number);
		else
			sieve_code_dumpf(denv, "NUM %llu", (unsigned long long) number);

		return TRUE;
	}
	
	return FALSE;
}

static bool opr_number_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, 
	sieve_number_t *number_r)
{ 
	return sieve_binary_read_integer(renv->sbin, address, number_r);
}

/* String */

void sieve_opr_string_emit(struct sieve_binary *sbin, string_t *str)
{
	(void) sieve_operand_emit_code(sbin, &string_operand);
	(void) sieve_binary_emit_string(sbin, str);
}

bool sieve_opr_string_dump_data
(const struct sieve_dumptime_env *denv, const struct sieve_operand *operand,
	sieve_size_t *address, const char *field_name) 
{
	const struct sieve_opr_string_interface *intf;
	
	if ( !sieve_operand_is_string(operand) ) {
		sieve_code_dumpf(denv, "ERROR: INVALID STRING OPERAND %s", operand->name);
		return FALSE;
	}
		
	intf = (const struct sieve_opr_string_interface *) operand->interface; 
	
	if ( intf->dump == NULL ) {
		sieve_code_dumpf(denv, "ERROR: DUMP STRING OPERAND");
		return FALSE;
	}

	return intf->dump(denv, address, field_name);  
}

bool sieve_opr_string_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	const struct sieve_operand *operand;
	
	sieve_code_mark(denv);
	operand = sieve_operand_read(denv->sbin, address);
	
	if ( operand == NULL ) {
		sieve_code_dumpf(denv, "ERROR: INVALID OPERAND");
		return FALSE;
	}

	return sieve_opr_string_dump_data(denv, operand, address, field_name);
}

bool sieve_opr_string_dump_ex
(const struct sieve_dumptime_env *denv, sieve_size_t *address, 
	const char *field_name, bool *literal_r)
{
	const struct sieve_operand *operand;
	
	sieve_code_mark(denv);
	operand = sieve_operand_read(denv->sbin, address);

	*literal_r = ( operand == &string_operand );	

	return sieve_opr_string_dump_data(denv, operand, address, field_name);
} 

bool sieve_opr_string_read_data
(const struct sieve_runtime_env *renv, const struct sieve_operand *operand,
	sieve_size_t *address, string_t **str_r)
{
	const struct sieve_opr_string_interface *intf;
	
	if ( operand == NULL || operand->class != &string_class ) 
		return FALSE;
		
	intf = (const struct sieve_opr_string_interface *) operand->interface; 
	
	if ( intf->read == NULL )
		return FALSE;

	return intf->read(renv, address, str_r);  
}

bool sieve_opr_string_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str_r)
{
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);

	return sieve_opr_string_read_data(renv, operand, address, str_r);
}

bool sieve_opr_string_read_ex
(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str_r,
	bool *literal_r)
{
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);

	*literal_r = ( operand == &string_operand );

	return sieve_opr_string_read_data(renv, operand, address, str_r);
}

static void _dump_string
(const struct sieve_dumptime_env *denv, string_t *str, 
	const char *field_name) 
{
	if ( str_len(str) > 80 ) {
		if ( field_name != NULL ) 
			sieve_code_dumpf(denv, "%s: STR[%ld] \"%s", 
				field_name, (long) str_len(str), str_sanitize(str_c(str), 80));
		else
			sieve_code_dumpf(denv, "STR[%ld] \"%s", 
				(long) str_len(str), str_sanitize(str_c(str), 80));
	} else {
		if ( field_name != NULL )
			sieve_code_dumpf(denv, "%s: STR[%ld] \"%s\"", 
				field_name, (long) str_len(str), str_sanitize(str_c(str), 80));		
		else
			sieve_code_dumpf(denv, "STR[%ld] \"%s\"", 
				(long) str_len(str), str_sanitize(str_c(str), 80));		
	}
}

bool opr_string_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	string_t *str; 
	
	if ( sieve_binary_read_string(denv->sbin, address, &str) ) {
		_dump_string(denv, str, field_name);   		
		
		return TRUE;
	}
  
	return FALSE;
}

static bool opr_string_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str_r)
{ 	
	return sieve_binary_read_string(renv->sbin, address, str_r);
}

/* String list */

void sieve_opr_stringlist_emit_start
	(struct sieve_binary *sbin, unsigned int listlen, void **context)
{
	sieve_size_t *end_offset = t_new(sieve_size_t, 1);

	/* Emit byte identifying the type of operand */	  
	(void) sieve_operand_emit_code(sbin, &stringlist_operand);
  
	/* Give the interpreter an easy way to skip over this string list */
	*end_offset = sieve_binary_emit_offset(sbin, 0);
	*context = (void *) end_offset;

	/* Emit the length of the list */
	(void) sieve_binary_emit_unsigned(sbin, listlen);
}

void sieve_opr_stringlist_emit_item
(struct sieve_binary *sbin, void *context ATTR_UNUSED, string_t *item)
{
	(void) sieve_opr_string_emit(sbin, item);
}

void sieve_opr_stringlist_emit_end
(struct sieve_binary *sbin, void *context)
{
	sieve_size_t *end_offset = (sieve_size_t *) context;

	(void) sieve_binary_resolve_offset(sbin, *end_offset);
}

bool sieve_opr_stringlist_dump_data
(const struct sieve_dumptime_env *denv, const struct sieve_operand *operand,
	sieve_size_t *address, const char *field_name) 
{
	if ( operand == NULL )
		return FALSE;
	
	if ( operand->class == &stringlist_class ) {
		const struct sieve_opr_stringlist_interface *intf =
			(const struct sieve_opr_stringlist_interface *) operand->interface; 
		
		if ( intf->dump == NULL )
			return FALSE;

		return intf->dump(denv, address, field_name); 
	} else if ( operand->class == &string_class ) {
		const struct sieve_opr_string_interface *intf =
			(const struct sieve_opr_string_interface *) operand->interface; 
	
		if ( intf->dump == NULL ) 
			return FALSE;

		return intf->dump(denv, address, field_name);  
	}
	
	return FALSE;
}

bool sieve_opr_stringlist_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	const struct sieve_operand *operand;

	sieve_code_mark(denv);
	operand = sieve_operand_read(denv->sbin, address);

	return sieve_opr_stringlist_dump_data(denv, operand, address, field_name);
}

struct sieve_coded_stringlist *sieve_opr_stringlist_read_data
(const struct sieve_runtime_env *renv, const struct sieve_operand *operand,
	sieve_size_t op_address, sieve_size_t *address)
{
	if ( operand == NULL )
		return NULL;
		
	if ( operand->class == &stringlist_class ) {
		const struct sieve_opr_stringlist_interface *intf = 
			(const struct sieve_opr_stringlist_interface *) operand->interface;
			
		if ( intf->read == NULL ) 
			return NULL;

		return intf->read(renv, address);  
	} else if ( operand->class == &string_class ) {
		/* Special case, accept single string as string list as well. */
		const struct sieve_opr_string_interface *intf = 
			(const struct sieve_opr_string_interface *) operand->interface;
				
		if ( intf->read == NULL || !intf->read(renv, address, NULL) ) {
			return NULL;
		}
		
		return sieve_coded_stringlist_create(renv, op_address, 1, *address); 
	}	
	
	return NULL;
}

struct sieve_coded_stringlist *sieve_opr_stringlist_read
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	sieve_size_t op_address = *address;
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);
	
	return sieve_opr_stringlist_read_data(renv, operand, op_address, address);
}

static bool opr_stringlist_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address, 
	const char *field_name) 
{
	sieve_size_t pc = *address;
	sieve_size_t end; 
	unsigned int length = 0; 
 	int end_offset;

	if ( !sieve_binary_read_offset(denv->sbin, address, &end_offset) )
		return FALSE;

	end = pc + end_offset;

	if ( !sieve_binary_read_unsigned(denv->sbin, address, &length) ) 
		return FALSE;	
  	
	return sieve_coded_stringlist_dump(denv, address, length, end, field_name); 
}

static struct sieve_coded_stringlist *opr_stringlist_read
(const struct sieve_runtime_env *renv, sieve_size_t *address )
{
	struct sieve_coded_stringlist *strlist;
	sieve_size_t pc = *address;
	sieve_size_t end; 
	unsigned int length = 0;  
	int end_offset;
	
	if ( !sieve_binary_read_offset(renv->sbin, address, &end_offset) )
		return NULL;

	end = pc + end_offset;

	if ( !sieve_binary_read_unsigned(renv->sbin, address, &length) ) 
	  	return NULL;	
  	
	strlist = sieve_coded_stringlist_create(renv, *address, (unsigned int) length, end); 

	/* Skip over the string list for now */
	*address = end;
  
	return strlist;
}  

/* Catenated String */

void sieve_opr_catenated_string_emit
(struct sieve_binary *sbin, unsigned int elements) 
{
	(void) sieve_operand_emit_code(sbin, &catenated_string_operand);
	(void) sieve_binary_emit_unsigned(sbin, elements);
}

static bool opr_catenated_string_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	unsigned int elements = 0;
	unsigned int i;
	
	if (!sieve_binary_read_unsigned(denv->sbin, address, &elements) )
		return FALSE;
	
	if ( field_name != NULL ) 
		sieve_code_dumpf(denv, "%s: CAT-STR [%ld]:", 
			field_name, (long) elements);
	else
		sieve_code_dumpf(denv, "CAT-STR [%ld]:", (long) elements);

	sieve_code_descend(denv);
	for ( i = 0; i < (unsigned int) elements; i++ ) {
		if ( !sieve_opr_string_dump(denv, address, NULL) )
			return FALSE;
	}
	sieve_code_ascend(denv);
	
	return TRUE;
}

static bool opr_catenated_string_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str)
{ 
	unsigned int elements = 0;
	unsigned int i;
		
	if ( !sieve_binary_read_unsigned(renv->sbin, address, &elements) )
		return FALSE;

	/* Parameter str can be NULL if we are requested to only skip and not 
	 * actually read the argument.
	 */
	if ( str == NULL ) {
		for ( i = 0; i < (unsigned int) elements; i++ ) {		
			if ( !sieve_opr_string_read(renv, address, NULL) ) 
				return FALSE;
		}
	} else {
		string_t *strelm;
		string_t **elm = &strelm;

		*str = t_str_new(128);
		for ( i = 0; i < (unsigned int) elements; i++ ) {
		
			if ( !sieve_opr_string_read(renv, address, elm) ) 
				return FALSE;
		
			if ( elm != NULL ) {
				str_append_str(*str, strelm);

				if ( str_len(*str) > SIEVE_MAX_STRING_LEN ) {
					str_truncate(*str, SIEVE_MAX_STRING_LEN);
					elm = NULL;
				}
			}
		}
	}

	return TRUE;
}

/* 
 * Core operations
 */
 
/* Forward declarations */

static bool opc_jmp_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);

static int opc_jmp_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);
static int opc_jmptrue_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);
static int opc_jmpfalse_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Operation objects defined in this file */

const struct sieve_operation sieve_jmp_operation = { 
	"JMP",
	NULL,
	SIEVE_OPERATION_JMP,
	opc_jmp_dump, 
	opc_jmp_execute 
};

const struct sieve_operation sieve_jmptrue_operation = { 
	"JMPTRUE",
	NULL,
	SIEVE_OPERATION_JMPTRUE,
	opc_jmp_dump, 
	opc_jmptrue_execute 
};

const struct sieve_operation sieve_jmpfalse_operation = { 
	"JMPFALSE",
	NULL,
	SIEVE_OPERATION_JMPFALSE,
	opc_jmp_dump, 
	opc_jmpfalse_execute 
};

/* Operation objects defined in other files */
	
extern const struct sieve_operation cmd_stop_operation;
extern const struct sieve_operation cmd_keep_operation;
extern const struct sieve_operation cmd_discard_operation;
extern const struct sieve_operation cmd_redirect_operation;

extern const struct sieve_operation tst_address_operation;
extern const struct sieve_operation tst_header_operation;
extern const struct sieve_operation tst_exists_operation;
extern const struct sieve_operation tst_size_over_operation;
extern const struct sieve_operation tst_size_under_operation;

const struct sieve_operation *sieve_operations[] = {
	NULL, 
	
	&sieve_jmp_operation,
	&sieve_jmptrue_operation, 
	&sieve_jmpfalse_operation,
	
	&cmd_stop_operation,
	&cmd_keep_operation,
	&cmd_discard_operation,
	&cmd_redirect_operation,

	&tst_address_operation,
	&tst_header_operation,
	&tst_exists_operation,
	&tst_size_over_operation,
	&tst_size_under_operation
}; 

const unsigned int sieve_operation_count =
	N_ELEMENTS(sieve_operations);

/* 
 * Operation functions 
 */

sieve_size_t sieve_operation_emit_code
(struct sieve_binary *sbin, const struct sieve_operation *op)
{
	sieve_size_t address;

    if ( op->extension != NULL ) {
        address = sieve_binary_emit_extension
            (sbin, op->extension, sieve_operation_count);

        sieve_binary_emit_extension_object
            (sbin, &op->extension->operations, op->code);

        return address;
    }

    return  sieve_binary_emit_byte(sbin, op->code);
}

const struct sieve_operation *sieve_operation_read
(struct sieve_binary *sbin, sieve_size_t *address) 
{
	const struct sieve_extension *ext;
	unsigned int code = sieve_operation_count;

	if ( !sieve_binary_read_extension(sbin, address, &code, &ext) )
		return NULL;

	if ( !ext )
		return code < sieve_operation_count ? sieve_operations[code] : NULL;

    return (const struct sieve_operation *) sieve_binary_read_extension_object
        (sbin, address, &ext->operations);
}

/*
 * Jump operations
 */
	
/* Code dump */

static bool opc_jmp_dump
(const struct sieve_operation *op,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	unsigned int pc = *address;
	int offset;
	
	if ( sieve_binary_read_offset(denv->sbin, address, &offset) ) 
		sieve_code_dumpf(denv, "%s %d [%08x]", 
			op->mnemonic, offset, pc + offset);
	else
		return FALSE;
	
	return TRUE;
}	
			
/* Code execution */

static int opc_jmp_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED) 
{
	sieve_runtime_trace(renv, "JMP");
	
	return sieve_interpreter_program_jump(renv->interp, TRUE);
}	
		
static int opc_jmptrue_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{	
	bool result = sieve_interpreter_get_test_result(renv->interp);
	
	sieve_runtime_trace(renv, "JMPTRUE (%s)", result ? "true" : "false");
	
	return sieve_interpreter_program_jump(renv->interp, result);
}

static int opc_jmpfalse_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{	
	bool result = sieve_interpreter_get_test_result(renv->interp);
	
	sieve_runtime_trace(renv, "JMPFALSE (%s)", result ? "true" : "false" );
	
	return sieve_interpreter_program_jump(renv->interp, !result);
}	
