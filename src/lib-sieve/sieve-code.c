#include "lib.h"
#include "str.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-extensions-private.h"
#include "sieve-actions.h"
#include "sieve-binary.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

#include "sieve-code.h"

#include <stdio.h>

/* 
 * Coded stringlist
 */

struct sieve_coded_stringlist {
	struct sieve_binary *binary;
	sieve_size_t start_address;
	sieve_size_t end_address;
	sieve_size_t current_offset;
	int length;
	int index;
};

static struct sieve_coded_stringlist *sieve_coded_stringlist_create
	(struct sieve_binary *sbin, 
	 sieve_size_t start_address, sieve_size_t length, sieve_size_t end)
{
	struct sieve_coded_stringlist *strlist;
	
	if ( end > sieve_binary_get_code_size(sbin) ) 
  		return NULL;
    
	strlist = p_new(pool_datastack_create(), struct sieve_coded_stringlist, 1);
	strlist->binary = sbin;
	strlist->start_address = start_address;
	strlist->current_offset = start_address;
	strlist->end_address = end;
	strlist->length = length;
	strlist->index = 0;
  
	return strlist;
}

bool sieve_coded_stringlist_next_item
	(struct sieve_coded_stringlist *strlist, string_t **str) 
{
	sieve_size_t address;
	*str = NULL;
  
	if ( strlist->index >= strlist->length ) 
		return TRUE;
	else {
		address = strlist->current_offset;
  	
		if ( sieve_opr_string_read(strlist->binary, &address, str) ) {
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

inline int sieve_coded_stringlist_get_length
	(struct sieve_coded_stringlist *strlist)
{
	return strlist->length;
}

inline sieve_size_t sieve_coded_stringlist_get_end_address
(struct sieve_coded_stringlist *strlist)
{
	return strlist->end_address;
}

inline sieve_size_t sieve_coded_stringlist_get_current_offset
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

/*
 * Operand functions
 */
 
inline sieve_size_t sieve_operand_emit_code
	(struct sieve_binary *sbin, int operand)
{
	unsigned char op = operand;
	
	return sieve_binary_emit_byte(sbin, op);
}
 
const struct sieve_operand *sieve_operand_read
	(struct sieve_binary *sbin, sieve_size_t *address) 
{
	unsigned int operand;
	
	if ( sieve_binary_read_byte(sbin, address, &operand) ) {
		if ( operand < SIEVE_OPERAND_CUSTOM ) {
			if ( operand < sieve_operand_count )
				return sieve_operands[operand];
			else
				return NULL;
		} else {
/*			int ext_id = -1;
		  const struct sieve_extension *ext = 
		  	sieve_binary_extension_get_by_index
		  		(sbin, operand - SIEVE_OPERAND_CUSTOM, &ext_id);
		  
		  if ( ext != NULL )
		  	return ext->operand;	
		  else*/
		  	return NULL;
		}
	}		
	
	return NULL;
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
 
/* Number */

static bool opr_number_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool opr_number_read
	(struct sieve_binary *sbin, sieve_size_t *address, sieve_size_t *number);

const struct sieve_opr_number_interface number_interface = { 
	opr_number_dump, 
	opr_number_read
};

const struct sieve_operand_class number_class = 
	{ "number", &number_interface };
	
const struct sieve_operand number_operand = 
	{ "@number", &number_class, TRUE };

/* String */

static bool opr_string_read
	(struct sieve_binary *sbin, sieve_size_t *address, string_t **str);
static bool opr_string_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

const struct sieve_opr_string_interface string_interface ={ 
	opr_string_dump,
	opr_string_read
};
	
const struct sieve_operand_class string_class = 
	{ "string", &string_interface };
	
const struct sieve_operand string_operand = 
	{ "@string", &string_class, TRUE };
	

/* String List */

static bool opr_stringlist_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static struct sieve_coded_stringlist *opr_stringlist_read
	(struct sieve_binary *sbin, sieve_size_t *address);

const struct sieve_opr_stringlist_interface stringlist_interface = { 
	opr_stringlist_dump, 
	opr_stringlist_read
};

const struct sieve_operand_class stringlist_class = 
	{ "string-list", &stringlist_interface };

const struct sieve_operand stringlist_operand = 
	{ "@string-list", &stringlist_class, TRUE };
	
/* Core operands */

extern struct sieve_operand comparator_operand;
extern struct sieve_operand match_type_operand;
extern struct sieve_operand address_part_operand;
extern struct sieve_operand side_effect_operand;

const struct sieve_operand *sieve_operands[] = {
	NULL, /* SIEVE_OPERAND_OPTIONAL */
	&number_operand,
	&string_operand,
	&stringlist_operand,
	&comparator_operand,
	&match_type_operand,
	&address_part_operand,
	&side_effect_operand
}; 

const unsigned int sieve_operand_count =
	N_ELEMENTS(sieve_operands);
	
/* 
 * Operand implementations 
 */
 
/* Number */

void sieve_opr_number_emit(struct sieve_binary *sbin, sieve_size_t number) 
{
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_NUMBER);
	(void) sieve_binary_emit_integer(sbin, number);
}

bool sieve_opr_number_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	const struct sieve_operand *operand;
	const struct sieve_opr_number_interface *intf;
	
	sieve_code_mark(denv);
	
	operand = sieve_operand_read(denv->sbin, address);

	if ( operand == NULL || operand->class != &number_class ) 
		return FALSE;
		
	intf = (const struct sieve_opr_number_interface *) operand->class->interface; 
	
	if ( intf->dump == NULL )
		return FALSE;

	return intf->dump(denv, address);  
}

bool sieve_opr_number_read
  (struct sieve_binary *sbin, sieve_size_t *address, sieve_size_t *number)
{
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	const struct sieve_opr_number_interface *intf;
	
	if ( operand == NULL || operand->class != &number_class ) 
		return FALSE;
		
	intf = (const struct sieve_opr_number_interface *) operand->class->interface; 
	
	if ( intf->read == NULL )
		return FALSE;

	return intf->read(sbin, address, number);  
}

static bool opr_number_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	sieve_size_t number = 0;
	
	if (sieve_binary_read_integer(denv->sbin, address, &number) ) {
		sieve_code_dumpf(denv, "NUM: %ld", (long) number);

		return TRUE;
	}
	
	return FALSE;
}

static bool opr_number_read
  (struct sieve_binary *sbin, sieve_size_t *address, sieve_size_t *number)
{ 
	return sieve_binary_read_integer(sbin, address, number);
}

/* String */

void sieve_opr_string_emit(struct sieve_binary *sbin, string_t *str)
{
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_STRING);
	(void) sieve_binary_emit_string(sbin, str);
}

bool sieve_opr_string_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	const struct sieve_operand *operand;
	const struct sieve_opr_string_interface *intf;
	
	sieve_code_mark(denv);
	operand = sieve_operand_read(denv->sbin, address);
	
	if ( operand == NULL || operand->class != &string_class ) 
		return FALSE;
		
	intf = (const struct sieve_opr_string_interface *) operand->class->interface; 
	
	if ( intf->dump == NULL ) 
		return FALSE;

	return intf->dump(denv, address);  
}

bool sieve_opr_string_read
  (struct sieve_binary *sbin, sieve_size_t *address, string_t **str)
{
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	const struct sieve_opr_string_interface *intf;
	
	if ( operand == NULL || operand->class != &string_class ) 
		return FALSE;
		
	intf = (const struct sieve_opr_string_interface *) operand->class->interface; 
	
	if ( intf->read == NULL )
		return FALSE;

	return intf->read(sbin, address, str);  
}

static void _dump_string
(const struct sieve_dumptime_env *denv, string_t *str) 
{
	if ( str_len(str) > 80 )
		sieve_code_dumpf(denv, "STR[%ld]: \"%s", 
			(long) str_len(str), str_sanitize(str_c(str), 80));
	else
		sieve_code_dumpf(denv, "STR[%ld]: \"%s\"", 
			(long) str_len(str), str_sanitize(str_c(str), 80));		
}

bool opr_string_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	string_t *str; 
	
	if ( sieve_binary_read_string(denv->sbin, address, &str) ) {
		_dump_string(denv, str);   		
		
		return TRUE;
	}
  
	return FALSE;
}

static bool opr_string_read
  (struct sieve_binary *sbin, sieve_size_t *address, string_t **str)
{ 	
	return sieve_binary_read_string(sbin, address, str);
}

/* String list */

void sieve_opr_stringlist_emit_start
	(struct sieve_binary *sbin, unsigned int listlen, void **context)
{
	sieve_size_t *end_offset = t_new(sieve_size_t, 1);

	/* Emit byte identifying the type of operand */	  
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_STRING_LIST);
  
	/* Give the interpreter an easy way to skip over this string list */
	*end_offset = sieve_binary_emit_offset(sbin, 0);
	*context = (void *) end_offset;

	/* Emit the length of the list */
	(void) sieve_binary_emit_integer(sbin, (int) listlen);
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

bool sieve_opr_stringlist_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	const struct sieve_operand *operand;

	sieve_code_mark(denv);
	operand = sieve_operand_read(denv->sbin, address);
		
	if ( operand == NULL )
		return FALSE;
	
	if ( operand->class == &stringlist_class ) {
		const struct sieve_opr_stringlist_interface *intf =
			(const struct sieve_opr_stringlist_interface *) operand->class->interface; 
		
		if ( intf->dump == NULL )
			return FALSE;

		return intf->dump(denv, address); 
	} else if ( operand->class == &string_class ) {
		const struct sieve_opr_string_interface *intf =
			(const struct sieve_opr_string_interface *) operand->class->interface; 
	
		if ( intf->dump == NULL ) 
			return FALSE;

		return intf->dump(denv, address);  
	}
	
	return FALSE;
}

struct sieve_coded_stringlist *sieve_opr_stringlist_read
  (struct sieve_binary *sbin, sieve_size_t *address)
{
	sieve_size_t start = *address;
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	
	if ( operand == NULL )
		return NULL;
		
	if ( operand->class == &stringlist_class ) {
		const struct sieve_opr_stringlist_interface *intf = 
			(const struct sieve_opr_stringlist_interface *) operand->class->interface;
			
		if ( intf->read == NULL ) 
			return NULL;

		return intf->read(sbin, address);  
	} else if ( operand->class == &string_class ) {
		/* Special case, accept single string as string list as well. */
		const struct sieve_opr_string_interface *intf = 
			(const struct sieve_opr_string_interface *) operand->class->interface;
		
  	if ( intf->read == NULL || !intf->read(sbin, address, NULL) ) {
  		printf("FAILED TO SKIP\n");
  		return NULL;
  	}
  
		return sieve_coded_stringlist_create(sbin, start, 1, *address); 
	}	
	
	return NULL;
}

static bool opr_stringlist_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	bool result = TRUE;
	struct sieve_coded_stringlist *strlist;
	
	if ( (strlist=opr_stringlist_read(denv->sbin, address)) != NULL ) {
		string_t *stritem;
		
		sieve_code_dumpf(denv, "STRLIST [%d] (END %08x)", 
			sieve_coded_stringlist_get_length(strlist), 
			sieve_coded_stringlist_get_end_address(strlist));
	  	
		sieve_code_mark_specific(denv,
			sieve_coded_stringlist_get_current_offset(strlist));
		sieve_code_descend(denv);
		while ( (result=sieve_coded_stringlist_next_item(strlist, &stritem)) && 
			stritem != NULL ) {	
			_dump_string(denv, stritem);
			sieve_code_mark_specific(denv,
				sieve_coded_stringlist_get_current_offset(strlist));  
		}
		sieve_code_ascend(denv);
		
		return result;
	}
	
	return FALSE;
}

static struct sieve_coded_stringlist *opr_stringlist_read
  (struct sieve_binary *sbin, sieve_size_t *address )
{
	struct sieve_coded_stringlist *strlist;

	sieve_size_t pc = *address;
	sieve_size_t end; 
	sieve_size_t length = 0; 
 
	int end_offset;
	
	if ( !sieve_binary_read_offset(sbin, address, &end_offset) )
		return NULL;

	end = pc + end_offset;

	if ( !sieve_binary_read_integer(sbin, address, &length) ) 
  	return NULL;	
  	
	strlist = sieve_coded_stringlist_create(sbin, *address, length, end); 

	/* Skip over the string list for now */
	*address = end;
  
	return strlist;
}  

/* 
 * Operations
 */
 
/* Declaration of opcodes defined in this file */

static bool opc_jmp_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);

static bool opc_jmp_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);
static bool opc_jmptrue_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);
static bool opc_jmpfalse_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

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

const unsigned int sieve_operations_count =
	N_ELEMENTS(sieve_operations);

static struct sieve_extension_obj_registry oprt_default_reg =
	SIEVE_EXT_DEFINE_OPERATIONS(sieve_operations);

/* Operation functions */

inline sieve_size_t sieve_operation_emit_code
	(struct sieve_binary *sbin, const struct sieve_operation *op, int ext_id)
{	
	return sieve_extension_emit_obj
		(sbin, &oprt_default_reg, op, operations, ext_id);
}

static const struct sieve_extension_obj_registry *
	sieve_operation_registry_get
(struct sieve_binary *sbin, unsigned int ext_index)
{
	int ext_id = -1; 
	const struct sieve_extension *ext;
	
	if ( (ext=sieve_binary_extension_get_by_index(sbin, ext_index, &ext_id)) 
		== NULL )
		return NULL;
		
	return &(ext->operations);
}

const struct sieve_operation *sieve_operation_read
	(struct sieve_binary *sbin, sieve_size_t *address) 
{
	return sieve_extension_read_obj
		(struct sieve_operation, sbin, address, &oprt_default_reg, 
			sieve_operation_registry_get);

/*	unsigned int opcode;
	
	if ( sieve_binary_read_byte(sbin, address, &opcode) ) {
		if ( opcode < SIEVE_OPERATION_CUSTOM ) {
			if ( opcode < sieve_operations_count )
				return sieve_operations[opcode];
			else
				return NULL;
		} else {
			struct sieve_operation *op;
			int ext_id = -1; 
			const struct sieve_extension *ext;
			
			if ( (ext=sieve_binary_extension_get_by_index
				(sbin, opcode - SIEVE_OPERATION_CUSTOM, &ext_id)) == NULL )
				return NULL;
	
			sieve_extension_read_object 
				(ext, struct sieve_operation, opcodes, sbin, address, op)

			return op;
		}
	}		
	
	return NULL;*/
}

	
/* Code dump for core commands */

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
			
/* Code dump for trivial operations */

bool sieve_operation_string_dump
(const struct sieve_operation *op,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "%s", op->mnemonic);

	return 
		sieve_opr_string_dump(denv, address);
}

/* Code execution for core commands */

static bool opc_jmp_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED) 
{
	printf("JMP\n");
	if ( !sieve_interpreter_program_jump(renv->interp, TRUE) )
		return FALSE;
	
	return TRUE;
}	
		
static bool opc_jmptrue_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{	
	if ( !sieve_interpreter_program_jump(renv->interp,
		sieve_interpreter_get_test_result(renv->interp)) )
		return FALSE;
		
	printf("JMPTRUE\n");
	
	return TRUE;
}

static bool opc_jmpfalse_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{	
	if ( !sieve_interpreter_program_jump(renv->interp,
		!sieve_interpreter_get_test_result(renv->interp)) )
		return FALSE;
		
	printf("JMPFALSE\n");
	
	return TRUE;
}	
