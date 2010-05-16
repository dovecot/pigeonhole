/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
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

/* 		return opr_r->def != NULL;
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
	
	if ( end > sieve_binary_block_get_size(renv->sblock) ) 
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
  	
		if ( sieve_opr_string_read(strlist->runenv, &address, NULL, str_r) ) {
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
	
	if ( end > sieve_binary_block_get_size(denv->sblock) ) 
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
 * Core operands
 */
 
extern const struct sieve_operand_def comparator_operand;
extern const struct sieve_operand_def match_type_operand;
extern const struct sieve_operand_def address_part_operand;

const struct sieve_operand_def *sieve_operands[] = {
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

sieve_size_t sieve_operand_emit
(struct sieve_binary_block *sblock, const struct sieve_extension *ext, 
	const struct sieve_operand_def *opr_def)
{
	sieve_size_t address;

	if ( ext != NULL ) {
		address = sieve_binary_emit_extension
			(sblock, ext, sieve_operand_count);
	
		sieve_binary_emit_extension_object
			(sblock, &opr_def->ext_def->operands, opr_def->code);

		return address;
	}

	return sieve_binary_emit_byte(sblock, opr_def->code);
}

bool sieve_operand_read
(struct sieve_binary_block *sblock, sieve_size_t *address, 
	struct sieve_operand *operand) 
{
	unsigned int code = sieve_operand_count;

	operand->address = *address;
	operand->ext = NULL;
	operand->def = NULL;

	if ( !sieve_binary_read_extension(sblock, address, &code, &operand->ext) )
		return NULL;

	if ( operand->ext == NULL ) {
		if ( code < sieve_operand_count )
			operand->def = sieve_operands[code];

		return ( operand->def != NULL );
	}

	if ( operand->ext->def == NULL )
		return FALSE;

	operand->def = (const struct sieve_operand_def *) 
		sieve_binary_read_extension_object(sblock, address, 
			&operand->ext->def->operands);

	return ( operand->def != NULL );
}

/*
 * Optional operand
 */

int sieve_opr_optional_next
(struct sieve_binary_block *sblock, sieve_size_t *address, signed int *opt_code)
{	
	/* Start of optional operand block */
	if ( *opt_code == 0 ) {
		sieve_size_t tmp_addr = *address;
		unsigned int op;
	
		if ( !sieve_binary_read_byte(sblock, &tmp_addr, &op) ||
			op != SIEVE_OPERAND_OPTIONAL )
			return 0;
	
		*address = tmp_addr;
	}

	/* Read optional operand code */
	if ( !sieve_binary_read_code(sblock, address, opt_code) ) 
		return -1;
	
	/* Return 0 at end of list */
	return ( *opt_code != 0 ? 1 : 0 );
}

/* 
 * Operand definitions
 */

/* Omitted */

const struct sieve_operand_class omitted_class =
	{ "OMITTED" };

const struct sieve_operand_def omitted_operand = {
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
	
const struct sieve_operand_def number_operand = { 
	"@number", 
	NULL, SIEVE_OPERAND_NUMBER,
	&number_class,
	&number_interface 
};

/* String */

static bool opr_string_dump
	(const struct sieve_dumptime_env *denv, const struct sieve_operand *opr,
		sieve_size_t *address, const char *field_name);
static bool opr_string_read
	(const struct sieve_runtime_env *renv, const struct sieve_operand *opr,
		sieve_size_t *address, string_t **str_r);

const struct sieve_opr_string_interface string_interface ={ 
	opr_string_dump,
	opr_string_read
};
	
const struct sieve_operand_class string_class = 
	{ "string" };
	
const struct sieve_operand_def string_operand = { 
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

const struct sieve_operand_def stringlist_operand =	{ 
	"@string-list", 
	NULL, SIEVE_OPERAND_STRING_LIST,
	&stringlist_class, 
	&stringlist_interface
};

/* Catenated String */

static bool opr_catenated_string_read
	(const struct sieve_runtime_env *renv, const struct sieve_operand *operand,
		sieve_size_t *address, string_t **str);
static bool opr_catenated_string_dump
	(const struct sieve_dumptime_env *denv, const struct sieve_operand *operand,
		sieve_size_t *address, const char *field_name);

const struct sieve_opr_string_interface catenated_string_interface = { 
	opr_catenated_string_dump,
	opr_catenated_string_read
};
		
const struct sieve_operand_def catenated_string_operand = { 
	"@catenated-string", 
	NULL, SIEVE_OPERAND_CATENATED_STRING,
	&string_class,
	&catenated_string_interface
};	
	
/* 
 * Operand implementations 
 */

/* Omitted */

void sieve_opr_omitted_emit(struct sieve_binary_block *sblock)
{
	(void) sieve_operand_emit(sblock, NULL, &omitted_operand);
}
 
/* Number */

void sieve_opr_number_emit
(struct sieve_binary_block *sblock, sieve_number_t number) 
{
	(void) sieve_operand_emit(sblock, NULL, &number_operand);
	(void) sieve_binary_emit_integer(sblock, number);
}

bool sieve_opr_number_dump_data
(const struct sieve_dumptime_env *denv, const struct sieve_operand *opr,
	sieve_size_t *address, const char *field_name) 
{
	const struct sieve_opr_number_interface *intf;

	if ( !sieve_operand_is_number(opr) ) 
		return FALSE;
		
	intf = (const struct sieve_opr_number_interface *) opr->def->interface; 
	
	if ( intf->dump == NULL )
		return FALSE;

	return intf->dump(denv, address, field_name);  
}

bool sieve_opr_number_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	struct sieve_operand operand;
	
	sieve_code_mark(denv);
	
	if ( !sieve_operand_read(denv->sblock, address, &operand) )
		return FALSE;

	return sieve_opr_number_dump_data(denv, &operand, address, field_name);
}

bool sieve_opr_number_read_data
(const struct sieve_runtime_env *renv, const struct sieve_operand *oprnd,
	sieve_size_t *address, const char *field_name, sieve_number_t *number_r)
{
	const struct sieve_opr_number_interface *intf;
		
	if ( !sieve_operand_is_number(oprnd) ) {
		sieve_runtime_trace_operand_error(renv, oprnd, field_name,
			"expected number operand but found %s", sieve_operand_name(oprnd));
		return FALSE;
	}
		
	intf = (const struct sieve_opr_number_interface *) oprnd->def->interface; 
	
	if ( intf->read == NULL )
		return FALSE;

	if ( !intf->read(renv, address, number_r) ) {
		sieve_runtime_trace_operand_error(renv, oprnd, field_name,
			"invalid number operand");
		return FALSE;	
	}

	return TRUE;
}

bool sieve_opr_number_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, 
	const char *field_name, sieve_number_t *number_r)
{
	struct sieve_operand operand;

	if ( !sieve_operand_runtime_read(renv, address, field_name, &operand) )
		return FALSE;
		
	return sieve_opr_number_read_data
		(renv, &operand, address, field_name, number_r);
}

static bool opr_number_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	sieve_number_t number = 0;
	
	if (sieve_binary_read_integer(denv->sblock, address, &number) ) {
		if ( field_name != NULL ) 
			sieve_code_dumpf(denv, "%s: NUM %llu", field_name, 
				(unsigned long long) number);
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
	return sieve_binary_read_integer(renv->sblock, address, number_r);
}

/* String */

void sieve_opr_string_emit(struct sieve_binary_block *sblock, string_t *str)
{
	(void) sieve_operand_emit(sblock, NULL, &string_operand);
	(void) sieve_binary_emit_string(sblock, str);
}

bool sieve_opr_string_dump_data
(const struct sieve_dumptime_env *denv, const struct sieve_operand *opr,
	sieve_size_t *address, const char *field_name) 
{
	const struct sieve_opr_string_interface *intf;
	
	if ( !sieve_operand_is_string(opr) ) {
		sieve_code_dumpf(denv, "ERROR: INVALID STRING OPERAND %s", 
			sieve_operand_name(opr));
		return FALSE;
	}
		
	intf = (const struct sieve_opr_string_interface *) opr->def->interface; 
	
	if ( intf->dump == NULL ) {
		sieve_code_dumpf(denv, "ERROR: DUMP STRING OPERAND");
		return FALSE;
	}

	return intf->dump(denv, opr, address, field_name);  
}

bool sieve_opr_string_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	struct sieve_operand operand;
	
	sieve_code_mark(denv);

	if ( !sieve_operand_read(denv->sblock, address, &operand) ) {
		sieve_code_dumpf(denv, "ERROR: INVALID OPERAND");
		return FALSE;
	}

	return sieve_opr_string_dump_data(denv, &operand, address, field_name);
}

bool sieve_opr_string_dump_ex
(const struct sieve_dumptime_env *denv, sieve_size_t *address, 
	const char *field_name, bool *literal_r)
{
	struct sieve_operand operand;
	
	sieve_code_mark(denv);
	if ( !sieve_operand_read(denv->sblock, address, &operand) ) {
		sieve_code_dumpf(denv, "ERROR: INVALID OPERAND");
		return FALSE;
	}

	*literal_r = sieve_operand_is(&operand, string_operand);	

	return sieve_opr_string_dump_data(denv, &operand, address, field_name);
} 

bool sieve_opr_string_read_data
(const struct sieve_runtime_env *renv, const struct sieve_operand *oprnd,
	sieve_size_t *address, const char *field_name, string_t **str_r)
{
	const struct sieve_opr_string_interface *intf;
	
	if ( !sieve_operand_is_string(oprnd) ) {
		sieve_runtime_trace_operand_error(renv, oprnd, field_name,
			"expected string operand but found %s", sieve_operand_name(oprnd));
		return FALSE;
	}
		
	intf = (const struct sieve_opr_string_interface *) oprnd->def->interface; 
	
	if ( intf->read == NULL ) {
		sieve_runtime_trace_operand_error(renv, oprnd, field_name,
			"invalid string operand");
		return FALSE;
	}

	return intf->read(renv, oprnd, address, str_r);  
}

bool sieve_opr_string_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, 
	const char *field_name, string_t **str_r)
{
	struct sieve_operand operand;

	if ( !sieve_operand_runtime_read(renv, address, field_name, &operand) )
		return FALSE;

	return sieve_opr_string_read_data(renv, &operand, address, field_name, str_r);
}

bool sieve_opr_string_read_ex
(const struct sieve_runtime_env *renv, sieve_size_t *address,
	const char *field_name, string_t **str_r, bool *literal_r)
{
	struct sieve_operand operand;

	if ( !sieve_operand_runtime_read(renv, address, field_name, &operand) )
		return FALSE;

	*literal_r = sieve_operand_is(&operand, string_operand);

	return sieve_opr_string_read_data(renv, &operand, address, field_name, str_r);
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
(const struct sieve_dumptime_env *denv, 
	const struct sieve_operand *opr ATTR_UNUSED, sieve_size_t *address, 
	const char *field_name) 
{
	string_t *str; 
	
	if ( sieve_binary_read_string(denv->sblock, address, &str) ) {
		_dump_string(denv, str, field_name);   		
		
		return TRUE;
	}
  
	return FALSE;
}

static bool opr_string_read
(const struct sieve_runtime_env *renv, 
	const struct sieve_operand *opr ATTR_UNUSED, sieve_size_t *address, 
	string_t **str_r)
{ 	
	return sieve_binary_read_string(renv->sblock, address, str_r);
}

/* String list */

void sieve_opr_stringlist_emit_start
(struct sieve_binary_block *sblock, unsigned int listlen, void **context)
{
	sieve_size_t *end_offset = t_new(sieve_size_t, 1);

	/* Emit byte identifying the type of operand */	  
	(void) sieve_operand_emit(sblock, NULL, &stringlist_operand);
  
	/* Give the interpreter an easy way to skip over this string list */
	*end_offset = sieve_binary_emit_offset(sblock, 0);
	*context = (void *) end_offset;

	/* Emit the length of the list */
	(void) sieve_binary_emit_unsigned(sblock, listlen);
}

void sieve_opr_stringlist_emit_item
(struct sieve_binary_block *sblock, void *context ATTR_UNUSED, string_t *item)
{
	(void) sieve_opr_string_emit(sblock, item);
}

void sieve_opr_stringlist_emit_end
(struct sieve_binary_block *sblock, void *context)
{
	sieve_size_t *end_offset = (sieve_size_t *) context;

	(void) sieve_binary_resolve_offset(sblock, *end_offset);
}

bool sieve_opr_stringlist_dump_data
(const struct sieve_dumptime_env *denv, const struct sieve_operand *opr,
	sieve_size_t *address, const char *field_name) 
{
	if ( opr == NULL || opr->def == NULL )
		return FALSE;
	
	if ( opr->def->class == &stringlist_class ) {
		const struct sieve_opr_stringlist_interface *intf =
			(const struct sieve_opr_stringlist_interface *) opr->def->interface; 
		
		if ( intf->dump == NULL )
			return FALSE;

		return intf->dump(denv, address, field_name); 
	} else if ( opr->def->class == &string_class ) {
		const struct sieve_opr_string_interface *intf =
			(const struct sieve_opr_string_interface *) opr->def->interface; 
	
		if ( intf->dump == NULL ) 
			return FALSE;

		return intf->dump(denv, opr, address, field_name);  
	}
	
	return FALSE;
}

bool sieve_opr_stringlist_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	struct sieve_operand operand;

	sieve_code_mark(denv);

	if ( !sieve_operand_read(denv->sblock, address, &operand) ) {
		return FALSE;
	}

	return sieve_opr_stringlist_dump_data(denv, &operand, address, field_name);
}

struct sieve_coded_stringlist *sieve_opr_stringlist_read_data
(const struct sieve_runtime_env *renv, const struct sieve_operand *oprnd,
	sieve_size_t *address, const char *field_name)
{
	if ( oprnd == NULL || oprnd->def == NULL )
		return NULL;
		
	if ( oprnd->def->class == &stringlist_class ) {
		const struct sieve_opr_stringlist_interface *intf = 
			(const struct sieve_opr_stringlist_interface *) oprnd->def->interface;
		struct sieve_coded_stringlist *strlist;
			
		if ( intf->read == NULL ) 
			return NULL;

		if ( (strlist=intf->read(renv, address)) == NULL ) {
			sieve_runtime_trace_operand_error(renv, oprnd, field_name,
				"invalid stringlist operand");
			return NULL;
		}

		return strlist;
	} else if ( oprnd->def->class == &string_class ) {
		/* Special case, accept single string as string list as well. */
		const struct sieve_opr_string_interface *intf = 
			(const struct sieve_opr_string_interface *) oprnd->def->interface;
				
		if ( intf->read == NULL || !intf->read(renv, oprnd, address, NULL) ) {
			sieve_runtime_trace_operand_error(renv, oprnd, field_name,
				"invalid stringlist string operand");
			return NULL;
		}
		
		return sieve_coded_stringlist_create(renv, oprnd->address, 1, *address); 
	}	

	sieve_runtime_trace_operand_error(renv, oprnd, field_name,
		"expected stringlist or string operand but found %s", 
		sieve_operand_name(oprnd));
	return NULL;
}

struct sieve_coded_stringlist *sieve_opr_stringlist_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, 
	const char *field_name)
{
	struct sieve_operand operand;

	if ( !sieve_operand_runtime_read(renv, address, field_name, &operand) )
		return NULL;
	
	return sieve_opr_stringlist_read_data(renv, &operand, address, field_name);
}

static bool opr_stringlist_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address, 
	const char *field_name) 
{
	sieve_size_t pc = *address;
	sieve_size_t end; 
	unsigned int length = 0; 
 	int end_offset;

	if ( !sieve_binary_read_offset(denv->sblock, address, &end_offset) )
		return FALSE;

	end = pc + end_offset;

	if ( !sieve_binary_read_unsigned(denv->sblock, address, &length) ) 
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
	
	if ( !sieve_binary_read_offset(renv->sblock, address, &end_offset) )
		return NULL;

	end = pc + end_offset;

	if ( !sieve_binary_read_unsigned(renv->sblock, address, &length) ) 
	  	return NULL;	
  	
	strlist = sieve_coded_stringlist_create(renv, *address, (unsigned int) length, end); 

	/* Skip over the string list for now */
	*address = end;
  
	return strlist;
}  

/* Catenated String */

void sieve_opr_catenated_string_emit
(struct sieve_binary_block *sblock, unsigned int elements) 
{
	(void) sieve_operand_emit(sblock, NULL, &catenated_string_operand);
	(void) sieve_binary_emit_unsigned(sblock, elements);
}

static bool opr_catenated_string_dump
(const struct sieve_dumptime_env *denv, 
	const struct sieve_operand *operand ATTR_UNUSED, sieve_size_t *address, 
	const char *field_name) 
{
	unsigned int elements = 0;
	unsigned int i;
	
	if (!sieve_binary_read_unsigned(denv->sblock, address, &elements) )
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
(const struct sieve_runtime_env *renv, 
	const struct sieve_operand *operand ATTR_UNUSED, sieve_size_t *address,
	string_t **str)
{ 
	unsigned int elements = 0;
	unsigned int i;
		
	if ( !sieve_binary_read_unsigned(renv->sblock, address, &elements) )
		return FALSE;

	/* Parameter str can be NULL if we are requested to only skip and not 
	 * actually read the argument.
	 */
	if ( str == NULL ) {
		for ( i = 0; i < (unsigned int) elements; i++ ) {		
			if ( !sieve_opr_string_read(renv, address, NULL, NULL) ) 
				return FALSE;
		}
	} else {
		string_t *strelm;
		string_t **elm = &strelm;

		*str = t_str_new(128);
		for ( i = 0; i < (unsigned int) elements; i++ ) {
		
			if ( !sieve_opr_string_read(renv, address, NULL, elm) ) 
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
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

static int opc_jmp_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);
static int opc_jmptrue_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);
static int opc_jmpfalse_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Operation objects defined in this file */

const struct sieve_operation_def sieve_jmp_operation = { 
	"JMP",
	NULL,
	SIEVE_OPERATION_JMP,
	opc_jmp_dump, 
	opc_jmp_execute 
};

const struct sieve_operation_def sieve_jmptrue_operation = { 
	"JMPTRUE",
	NULL,
	SIEVE_OPERATION_JMPTRUE,
	opc_jmp_dump, 
	opc_jmptrue_execute 
};

const struct sieve_operation_def sieve_jmpfalse_operation = { 
	"JMPFALSE",
	NULL,
	SIEVE_OPERATION_JMPFALSE,
	opc_jmp_dump, 
	opc_jmpfalse_execute 
};

/* Operation objects defined in other files */
	
extern const struct sieve_operation_def cmd_stop_operation;
extern const struct sieve_operation_def cmd_keep_operation;
extern const struct sieve_operation_def cmd_discard_operation;
extern const struct sieve_operation_def cmd_redirect_operation;

extern const struct sieve_operation_def tst_address_operation;
extern const struct sieve_operation_def tst_header_operation;
extern const struct sieve_operation_def tst_exists_operation;
extern const struct sieve_operation_def tst_size_over_operation;
extern const struct sieve_operation_def tst_size_under_operation;

const struct sieve_operation_def *sieve_operations[] = {
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

sieve_size_t sieve_operation_emit
(struct sieve_binary_block *sblock, const struct sieve_extension *ext,
	const struct sieve_operation_def *op_def)
{
	sieve_size_t address;

  if ( ext != NULL ) {
		address = sieve_binary_emit_extension
			(sblock, ext, sieve_operation_count);

		sieve_binary_emit_extension_object
			(sblock, &op_def->ext_def->operations, op_def->code);

		return address;
  }

  return sieve_binary_emit_byte(sblock, op_def->code);
}

bool sieve_operation_read
(struct sieve_binary_block *sblock, sieve_size_t *address,
	struct sieve_operation *oprtn) 
{
	unsigned int code = sieve_operation_count;

	oprtn->address = *address;
	oprtn->def = NULL;
	oprtn->ext = NULL;

	if ( !sieve_binary_read_extension(sblock, address, &code, &oprtn->ext) )
		return FALSE;

	if ( !oprtn->ext ) {
		if ( code < sieve_operation_count ) {
			oprtn->def = sieve_operations[code];
		}

		return ( oprtn->def != NULL );
	}

	oprtn->def = (const struct sieve_operation_def *) 
		sieve_binary_read_extension_object(sblock, address, 
			&oprtn->ext->def->operations);

	return ( oprtn->def != NULL );
}

/*
 * Jump operations
 */
	
/* Code dump */

static bool opc_jmp_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct sieve_operation *oprtn = denv->oprtn;
	unsigned int pc = *address;
	int offset;
	
	if ( sieve_binary_read_offset(denv->sblock, address, &offset) ) 
		sieve_code_dumpf(denv, "%s %d [%08x]", 
			sieve_operation_mnemonic(oprtn), offset, pc + offset);
	else
		return FALSE;
	
	return TRUE;
}	
			
/* Code execution */

static int opc_jmp_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED) 
{
	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS, "jump");
	
	return sieve_interpreter_program_jump(renv->interp, TRUE);
}	
		
static int opc_jmptrue_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{	
	bool result = sieve_interpreter_get_test_result(renv->interp);
	
	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS, "jump if true (%s)", 
		result ? "true" : "false");
	
	return sieve_interpreter_program_jump(renv->interp, result);
}

static int opc_jmpfalse_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{	
	bool result = sieve_interpreter_get_test_result(renv->interp);
	
	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS, "jump if false (%s)",
		result ? "true" : "false" );
	
	return sieve_interpreter_program_jump(renv->interp, !result);
}	
